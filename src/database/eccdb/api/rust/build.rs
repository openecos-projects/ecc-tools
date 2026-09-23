use std::{env, path::PathBuf, process::Command};
fn run(command: &mut Command) {
    let status = command.status().expect("cannot run C ABI layout compiler");
    assert!(status.success(), "C ABI layout compilation failed");
}
fn main() {
    println!("cargo:rerun-if-env-changed=ECCDB_LIB_DIR");
    println!("cargo:rerun-if-env-changed=ECCDB_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");
    println!("cargo:rerun-if-changed=native");
    let lib=PathBuf::from(env::var_os("ECCDB_LIB_DIR").expect("Set ECCDB_LIB_DIR to the installed ECCDB SDK lib directory (containing libeccdb_c.so and libeccdb.so)"));
    assert!(lib.is_absolute(), "ECCDB_LIB_DIR must be absolute");
    println!("cargo:rustc-link-search=native={}", lib.display());
    println!("cargo:rustc-link-lib=dylib=eccdb_c");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let include = env::var_os("ECCDB_INCLUDE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("native"));
    println!(
        "cargo:rerun-if-changed={}",
        include.join("eccdb_c/eccdb.h").display()
    );
    run(
        Command::new(env::var_os("CC").unwrap_or_else(|| "cc".into()))
            .arg("-std=c11")
            .arg("-fPIC")
            .arg("-I")
            .arg(include)
            .arg("-c")
            .arg("native/layout.c")
            .arg("-o")
            .arg(out.join("layout.o")),
    );
    run(
        Command::new(env::var_os("AR").unwrap_or_else(|| "ar".into()))
            .arg("crs")
            .arg(out.join("libeccdb_rust_layout.a"))
            .arg(out.join("layout.o")),
    );
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=eccdb_rust_layout");
}
