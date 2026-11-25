//! msdevbox — command line interface for the Dev Box pipeline.
//!
//!   msdevbox login --upn you@tenant.com [--scope rdweb|enduser]
//!   msdevbox list --upn you@tenant.com [--all]
//!   msdevbox rdp  --upn you@tenant.com --tenant GUID --resource GUID [-o FILE]
//!   msdevbox rdp  --upn you@tenant.com --launch URL [-o FILE]
//!
//! Tokens are never printed. The login flow prints the authorize URL; open
//! it in a browser (or point an embedded web view at it), then paste the
//! FULL landing URL back.

use msdevbox::{auth, discovery, rdpfile};

use std::io::{self, BufRead, Write};

fn out_err(msg: &str) {
    eprintln!("{msg}");
}

fn scope_from_name(name: &str) -> Option<&'static str> {
    match name.to_ascii_lowercase().as_str() {
        "rdweb" => Some(msdevbox::SCOPE_RDWEB),
        "enduser" => Some(msdevbox::SCOPE_ENDUSER),
        _ => None,
    }
}

fn cmd_login(upn: &str, scope_name: &str) -> i32 {
    let Some(scope) = scope_from_name(scope_name) else {
        out_err(&format!("unknown scope: {scope_name}"));
        return 2;
    };
    let session = auth::begin(upn, scope);
    println!("Open this URL in a browser and sign in:\n\n{}\n", session.authorize_url);
    println!("After sign-in you land on a nativeclient page.");
    print!("Paste the FULL redirect URL here: ");
    let _ = io::stdout().flush();

    let stdin = io::stdin();
    let pasted = match stdin.lock().lines().next() {
        Some(Ok(line)) => line.trim().to_string(),
        _ => String::new(),
    };
    if pasted.is_empty() {
        out_err("no URL given");
        return 1;
    }
    match auth::complete(&session, &pasted) {
        Ok(_) => {
            println!("login complete; token cached (never displayed)");
            0
        }
        Err(e) => {
            out_err(&format!("login failed: {e}"));
            1
        }
    }
}

fn cmd_list(all: bool) -> i32 {
    let token = match auth::access_token(msdevbox::SCOPE_ENDUSER) {
        Ok(token) => token,
        Err(_) => {
            out_err("no cached EndUser token; run: msdevbox login --upn (your upn) --scope enduser");
            return 1;
        }
    };
    match discovery::list_dev_boxes(&token.access_token, all) {
        Ok(boxes) => {
            println!("name\tworkspace\tstate\ttenant/resource");
            for box_info in &boxes {
                let tenant = box_info.avd_tenant.as_deref().unwrap_or("-");
                let resource = box_info.avd_resource.as_deref().unwrap_or("-");
                println!(
                    "{}\t{}\t{}\t{}/{}",
                    box_info.display_name, box_info.workspace_name, box_info.connection_state, tenant, resource
                );
            }
            if boxes.is_empty() {
                println!("(no Dev Boxes visible)");
            }
            0
        }
        Err(e) => {
            out_err(&format!("listing failed: {e}"));
            if e.contains("401") || e.contains("403") {
                eprintln!("the EndUser.Access scope may not be grantable to the RD Web client");
                eprintln!("identity from this tenant; the rdp command (rdweb scope) works regardless");
            }
            1
        }
    }
}

fn cmd_rdp(upn: &str, tenant: &str, resource: &str, launch_url: &str, out_path: Option<&str>) -> i32 {
    let token = match auth::access_token(msdevbox::SCOPE_RDWEB) {
        Ok(token) => token,
        Err(_) => {
            out_err(&format!("no cached RDWeb token; run: msdevbox login --upn {upn}"));
            return 1;
        }
    };
    let rdp = if !launch_url.is_empty() {
        discovery::fetch_rdp_file_from_launch_url(&token.access_token, launch_url)
    } else if !tenant.is_empty() && !resource.is_empty() {
        discovery::fetch_rdp_file(&token.access_token, tenant, resource)
    } else {
        out_err("need --tenant and --resource, or --launch URL");
        return 2;
    };
    let rdp = match rdp {
        Ok(rdp) => rdp,
        Err(e) => {
            out_err(&format!("rdp fetch failed: {e}"));
            return 1;
        }
    };
    match rdpfile::save(&rdp, out_path) {
        Ok(path) => {
            println!(
                "saved: {path} ({} bytes, DevBox routing: {})",
                rdp.len(),
                if rdpfile::is_devbox_rdp(&rdp) { "yes" } else { "no" }
            );
            println!("connect with:\n  xfreerdp3 {path} /gateway:type:arm /sec:aad /cert:tofu /u:{upn}");
            0
        }
        Err(e) => {
            out_err(&format!("save failed: {e}"));
            1
        }
    }
}

struct Args {
    upn: String,
    scope: String,
    all: bool,
    tenant: String,
    resource: String,
    launch: String,
    out: Option<String>,
}

fn usage() -> i32 {
    eprintln!("usage: msdevbox login --upn UPN [--scope rdweb|enduser]");
    eprintln!("       msdevbox list --upn UPN [--all]");
    eprintln!("       msdevbox rdp --upn UPN (--tenant GUID --resource GUID | --launch URL) [-o FILE]");
    2
}

fn parse_args(args: &[String]) -> Result<(String, Args), String> {
    let mut command = String::new();
    let mut parsed = Args {
        upn: String::new(),
        scope: String::new(),
        all: false,
        tenant: String::new(),
        resource: String::new(),
        launch: String::new(),
        out: None,
    };
    let mut index = 0;
    while index < args.len() {
        let arg = &args[index];
        macro_rules! value {
            () => {{
                index += 1;
                args.get(index).cloned().ok_or_else(|| format!("missing value for {arg}"))?
            }};
        }
        match arg.as_str() {
            "--upn" | "-u" => parsed.upn = value!(),
            "--scope" => parsed.scope = value!(),
            "--all" | "-a" => parsed.all = true,
            "--tenant" => parsed.tenant = value!(),
            "--resource" => parsed.resource = value!(),
            "--launch" => parsed.launch = value!(),
            "--out" | "-o" => parsed.out = Some(value!()),
            "--help" | "-h" => return Err("help".to_string()),
            other => {
                if other.starts_with("--") {
                    return Err(format!("unknown option: {other}"));
                }
                if !command.is_empty() {
                    return Err(format!("unexpected argument: {other}"));
                }
                command = other.to_string();
            }
        }
        index += 1;
    }
    if parsed.scope.is_empty() {
        parsed.scope = "rdweb".to_string();
    }
    Ok((command, parsed))
}

fn main() {
    let raw: Vec<String> = std::env::args().skip(1).collect();
    let Ok((command, args)) = parse_args(&raw) else {
        usage();
        return;
    };
    if command.is_empty() {
        usage();
        return;
    }
    if args.upn.is_empty() {
        out_err("--upn is required");
        std::process::exit(2);
    }
    let code = match command.as_str() {
        "login" => {
            if args.tenant.is_empty() {
                cmd_login(&args.upn, &args.scope)
            } else {
                usage()
            }
        }
        "list" => cmd_list(args.all),
        "rdp" => cmd_rdp(&args.upn, &args.tenant, &args.resource, &args.launch, args.out.as_deref()),
        other => {
            out_err(&format!("unknown command: {other}"));
            usage()
        }
    };
    std::process::exit(code);
}
