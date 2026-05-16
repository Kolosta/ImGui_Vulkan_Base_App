use std::env;
use std::path::PathBuf;

fn main() {
    let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();

    // Where to write the generated C header.
    //
    // CMake passes an absolute, deterministic path via RESVG_HEADER_OUT so
    // the C++ side has a single source of truth in the build tree. When
    // built standalone (plain `cargo build`), fall back to the historical
    // <crate>/include/resvg_c.h location.
    let output_file: PathBuf = match env::var("RESVG_HEADER_OUT") {
        Ok(p) if !p.is_empty() => PathBuf::from(p),
        _ => PathBuf::from(&crate_dir).join("include").join("resvg_c.h"),
    };
    if let Some(parent) = output_file.parent() {
        std::fs::create_dir_all(parent).ok();
    }

    cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_language(cbindgen::Language::C)
        .with_include_guard("RESVG_C_H")
        .with_pragma_once(true)
        .with_namespace("resvg")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file(&output_file);

    // Re-run whenever the FFI surface, this script, or the requested
    // output location changes (so a missing/moved header is regenerated).
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=RESVG_HEADER_OUT");
}
