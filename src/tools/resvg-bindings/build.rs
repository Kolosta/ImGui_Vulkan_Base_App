// extern crate cbindgen;

// use std::env;
// use std::path::PathBuf;

// fn main() {
//     let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
//     let output_file = PathBuf::from(&crate_dir)
//         .join("include")
//         .join("resvg_c.h");

//     // Ensure include directory exists
//     std::fs::create_dir_all(PathBuf::from(&crate_dir).join("include")).ok();

//     cbindgen::Builder::new()
//         .with_crate(crate_dir)
//         .with_language(cbindgen::Language::C)
//         .with_include_guard("RESVG_C_H")
//         .generate()
//         .expect("Unable to generate bindings")
//         .write_to_file(output_file);
// }



use std::env;

fn main() {
    let crate_dir = env::var("CARGO_MANIFEST_DIR").unwrap();

    // Generate C header file
    cbindgen::Builder::new()
        .with_crate(crate_dir)
        .with_language(cbindgen::Language::C)
        .with_include_guard("RESVG_C_H")
        .with_pragma_once(true)
        .with_namespace("resvg")
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("include/resvg_c.h");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=build.rs");
}