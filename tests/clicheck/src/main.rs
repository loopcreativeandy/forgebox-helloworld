//! Drives the ForgeBox emulator (or anything speaking [len][packet] on stdio) with the exact
//! request/response code of agave remote-wallet/src/keystone.rs, minus rusb:
//!   pubkey PATH            -> CmdGetDeviceUSBPubkey (0x06), prints base58-less hex pubkey
//!   sign PATH MESSAGE_HEX  -> CmdResolveUR (0x02) with a sol-sign-request UR, prints signature hex
//! PATH like 44'/501'/0'/0'  (all hardened, as solana DerivationPath produces)
use std::io::{Read, Write};
use std::process::{Command, Stdio};
use ur_parse_lib::{keystone_ur_decoder::probe_decode, keystone_ur_encoder::probe_encode};
use ur_registry::{
    crypto_key_path::{CryptoKeyPath, PathComponent},
    solana::{sol_sign_request::{SignType, SolSignRequest}, sol_signature::SolSignature},
    traits::RegistryItem,
};

const OFFSET_CDATA: usize = 9;
const MAX_REQ_DATA: usize = 64 - OFFSET_CDATA;
const REQUEST_ID: u16 = 0x0000;
const MAX_UR_FRAGMENT_LEN: usize = 0x0FFF_FFFF;

struct Emu { child: std::process::Child }

impl Emu {
    fn write_packet(&mut self, p: &[u8]) {
        let s = self.child.stdin.as_mut().unwrap();
        s.write_all(&[p.len() as u8]).unwrap();
        s.write_all(p).unwrap();
        s.flush().unwrap();
    }
    fn read_packet(&mut self) -> Vec<u8> {
        let o = self.child.stdout.as_mut().unwrap();
        let mut n = [0u8; 1];
        o.read_exact(&mut n).unwrap();
        let mut b = vec![0u8; n[0] as usize];
        o.read_exact(&mut b).unwrap();
        b
    }
    // keystone.rs write()
    fn write(&mut self, command: u16, data: &[u8]) {
        let total = std::cmp::max(1, data.len().div_ceil(MAX_REQ_DATA));
        for i in 0..total {
            let chunk = &data[i * MAX_REQ_DATA..std::cmp::min((i + 1) * MAX_REQ_DATA, data.len())];
            let mut p = vec![0u8; OFFSET_CDATA + chunk.len()];
            p[1..3].copy_from_slice(&command.to_be_bytes());
            p[3..5].copy_from_slice(&(total as u16).to_be_bytes());
            p[5..7].copy_from_slice(&(i as u16).to_be_bytes());
            p[7..9].copy_from_slice(&REQUEST_ID.to_be_bytes());
            p[9..].copy_from_slice(chunk);
            self.write_packet(&p);
        }
    }
    // keystone.rs read(), condensed
    fn read(&mut self) -> Result<Vec<u8>, String> {
        let mut chunks: Vec<Option<Vec<u8>>> = Vec::new();
        let mut status = None;
        loop {
            let p = self.read_packet();
            let cmd = u16::from_be_bytes([p[1], p[2]]);
            let total = u16::from_be_bytes([p[3], p[4]]) as usize;
            let seq = u16::from_be_bytes([p[5], p[6]]) as usize;
            if !(0x01..=0x06).contains(&cmd) || total == 0 || seq >= total {
                return Err("Unable to parse packet header".into());
            }
            if chunks.is_empty() { chunks = vec![None; total]; }
            let payload = &p[OFFSET_CDATA..];
            let plen = payload.len() - 2;
            status = Some(u16::from_be_bytes([payload[plen], payload[plen + 1]]));
            chunks[seq] = Some(payload[..plen].to_vec());
            if chunks.iter().all(|c| c.is_some()) { break; }
        }
        let data: Vec<u8> = chunks.into_iter().flat_map(|c| c.unwrap()).collect();
        if status != Some(0) {
            return Err(format!("status {:04x}: {}", status.unwrap(), String::from_utf8_lossy(&data)));
        }
        Ok(data)
    }
    fn send_apdu(&mut self, command: u16, data: &[u8]) -> Result<String, String> {
        self.write(command, data);
        let s = String::from_utf8_lossy(&self.read()?).to_string();
        match (s.find('{'), s.rfind('}')) {
            (Some(a), Some(b)) if a < b => Ok(s[a..=b].to_string()),
            _ => Ok(s),
        }
    }
}

fn parse_path(p: &str) -> Vec<u32> {
    p.trim_start_matches("m/").split('/').map(|c| {
        let h = c.ends_with('\'');
        c.trim_end_matches('\'').parse::<u32>().unwrap() | if h { 0x8000_0000 } else { 0 }
    }).collect()
}

fn field(json: &str, name: &str) -> String {
    let v: serde_json::Value = serde_json::from_str(json).expect("json");
    v.get(name).and_then(|x| x.as_str()).expect("field").to_string()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let emu = std::env::var("FB_EMU").unwrap_or("/tmp/fb_emu".into());
    let mut e = Emu { child: Command::new(emu).stdin(Stdio::piped()).stdout(Stdio::piped()).spawn().unwrap() };
    let path = parse_path(&args[2]);
    match args[1].as_str() {
        "pubkey" => {
            // keystone.rs extend_and_serialize()
            let mut s = 501u32.to_be_bytes().to_vec();
            s.push(path.len() as u8);
            for i in &path { s.extend_from_slice(&i.to_be_bytes()); }
            let j = e.send_apdu(0x06, &s).unwrap();
            println!("{}", field(&j, "pubkey"));
        }
        "sign" => {
            let data = hex::decode(&args[3]).unwrap();
            // keystone.rs parse_crypto_key_path() + generate_sol_sign_request()
            let comps = path.iter().map(|b| PathComponent::new(Some(b & 0x7fff_ffff), b & 0x8000_0000 != 0).unwrap()).collect();
            let kp = CryptoKeyPath::new(comps, Some([0, 0, 0, 0]), None);
            let req = SolSignRequest::new(Some([0u8; 16].to_vec()), data, kp, None, Some("solana cli".to_string()), SignType::Transaction);
            let bytes: Vec<u8> = req.try_into().unwrap();
            let ur = probe_encode(&bytes, MAX_UR_FRAGMENT_LEN, SolSignRequest::get_registry_type().get_type()).unwrap().data;
            eprintln!("request UR ({} chars): {}...", ur.len(), &ur[..60.min(ur.len())]);
            match e.send_apdu(0x02, ur.as_bytes()) {
                Ok(j) => {
                    let sig_ur = field(&j, "payload");
                    eprintln!("response UR: {}...", &sig_ur[..60.min(sig_ur.len())]);
                    // keystone.rs parse_ur_signature()
                    let r: ur_parse_lib::keystone_ur_decoder::URParseResult<SolSignature> = probe_decode(sig_ur.to_lowercase()).unwrap();
                    println!("{}", hex::encode(r.data.unwrap().get_signature()));
                }
                Err(err) => { println!("ERROR {err}"); std::process::exit(2); }
            }
        }
        _ => panic!("usage"),
    }
}
