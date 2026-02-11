use std::ffi::CStr;
use std::os::raw::c_char;
use std::ptr;
use std::slice;

pub use resvg::usvg;
pub use resvg::tiny_skia;

// =========================
// Types C opaques
// =========================

#[repr(C)]
pub struct resvg_options {
    _private: [u8; 0],
}

#[repr(C)]
pub struct resvg_render_tree {
    _private: [u8; 0],
}

// =========================
// Types internes Rust
// =========================

struct RenderTree {
    tree: usvg::Tree,
}

// Alias FFI : CE TYPE EST EXPOSÉ AU C
type OpaqueTree = RenderTree;

// =========================
// Structs POD C
// =========================

#[repr(C)]
pub struct resvg_size {
    pub width: f32,
    pub height: f32,
}

#[repr(C)]
pub struct resvg_transform {
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
    pub e: f32,
    pub f: f32,
}

#[repr(C)]
#[allow(non_camel_case_types)]
pub enum resvg_fit_to_type {
    RESVG_FIT_TO_TYPE_ORIGINAL,
    RESVG_FIT_TO_TYPE_WIDTH,
    RESVG_FIT_TO_TYPE_HEIGHT,
    RESVG_FIT_TO_TYPE_ZOOM,
}

#[repr(C)]
pub struct resvg_fit_to {
    pub type_: resvg_fit_to_type,
    pub value: f32,
}

// =========================
// API C
// =========================

#[no_mangle]
pub extern "C" fn resvg_init_log() {}

#[no_mangle]
pub extern "C" fn resvg_options_create() -> *mut resvg_options {
    Box::into_raw(Box::new([0u8; 1])) as *mut resvg_options
}

#[no_mangle]
pub extern "C" fn resvg_options_load_system_fonts(_opt: *mut resvg_options) {}

#[no_mangle]
pub extern "C" fn resvg_options_destroy(opt: *mut resvg_options) {
    if !opt.is_null() {
        unsafe { drop(Box::from_raw(opt as *mut u8)); }
    }
}

#[no_mangle]
pub extern "C" fn resvg_parse_tree_from_data(
    data: *const c_char,
    len: usize,
    _opt: *const resvg_options,
) -> *mut resvg_render_tree {
    if data.is_null() {
        return ptr::null_mut();
    }

    let bytes = unsafe { slice::from_raw_parts(data as *const u8, len) };
    let svg = match std::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let opt = usvg::Options::default();

    match usvg::Tree::from_str(svg, &opt) {
        Ok(tree) => {
            let boxed = Box::new(OpaqueTree { tree });
            Box::into_raw(boxed) as *mut resvg_render_tree
        }
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn resvg_get_image_size(
    tree: *const resvg_render_tree,
) -> resvg_size {
    if tree.is_null() {
        return resvg_size { width: 0.0, height: 0.0 };
    }

    let tree = unsafe { &*(tree as *const OpaqueTree) };
    let size = tree.tree.size();

    resvg_size {
        width: size.width(),
        height: size.height(),
    }
}

#[no_mangle]
pub extern "C" fn resvg_transform_identity() -> resvg_transform {
    resvg_transform {
        a: 1.0, b: 0.0,
        c: 0.0, d: 1.0,
        e: 0.0, f: 0.0,
    }
}

#[no_mangle]
pub extern "C" fn resvg_render(
    tree: *const resvg_render_tree,
    fit_to: resvg_fit_to,
    transform: resvg_transform,
    width: u32,
    height: u32,
    pixmap: *mut u8,
) {
    if tree.is_null() || pixmap.is_null() {
        return;
    }

    let tree = unsafe { &*(tree as *const OpaqueTree) };

    let mut pixmap_ts = match tiny_skia::Pixmap::new(width, height) {
        Some(p) => p,
        None => return,
    };

    let ts = tiny_skia::Transform::from_row(
        transform.a, transform.b,
        transform.c, transform.d,
        transform.e, transform.f,
    );

    let ts = match fit_to.type_ {
        resvg_fit_to_type::RESVG_FIT_TO_TYPE_ZOOM => ts.pre_scale(fit_to.value, fit_to.value),
        resvg_fit_to_type::RESVG_FIT_TO_TYPE_WIDTH => {
            let s = width as f32 / tree.tree.size().width();
            ts.pre_scale(s, s)
        }
        resvg_fit_to_type::RESVG_FIT_TO_TYPE_HEIGHT => {
            let s = height as f32 / tree.tree.size().height();
            ts.pre_scale(s, s)
        }
        _ => ts,
    };

    resvg::render(&tree.tree, ts, &mut pixmap_ts.as_mut());

    unsafe {
        ptr::copy_nonoverlapping(
            pixmap_ts.data().as_ptr(),
            pixmap,
            (width * height * 4) as usize,
        );
    }
}

#[no_mangle]
pub extern "C" fn resvg_tree_destroy(tree: *mut resvg_render_tree) {
    if !tree.is_null() {
        unsafe {
            drop(Box::from_raw(tree as *mut OpaqueTree));
        }
    }
}
