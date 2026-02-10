use std::os::raw::{c_char, c_uint, c_float};
use std::ptr;
use std::slice;

use usvg::{Options, Tree};
use tiny_skia::Pixmap;

// Opaque types
pub struct ResvgOptions {
    usvg_options: Options<'static>,
}

pub struct ResvgRenderTree {
    tree: Tree,
}

#[repr(C)]
pub struct ResvgSize {
    pub width: c_float,
    pub height: c_float,
}

#[repr(C)]
pub struct ResvgTransform {
    pub a: c_float,
    pub b: c_float,
    pub c: c_float,
    pub d: c_float,
    pub e: c_float,
    pub f: c_float,
}

#[repr(C)]
pub enum ResvgFitToType {
    Original,
    Width,
    Height,
    Zoom,
}

#[repr(C)]
pub struct ResvgFitTo {
    pub fit_type: ResvgFitToType,
    pub value: c_float,
}

// Create options
#[no_mangle]
pub extern "C" fn resvg_options_create() -> *mut ResvgOptions {
    let options = Box::new(ResvgOptions {
        usvg_options: Options::default(),
    });
    Box::into_raw(options)
}

// Load system fonts (no-op in C API, fonts are loaded by default)
#[no_mangle]
pub extern "C" fn resvg_options_load_system_fonts(_options: *mut ResvgOptions) {
    // No-op: usvg loads system fonts by default
}

// Destroy options
#[no_mangle]
pub extern "C" fn resvg_options_destroy(options: *mut ResvgOptions) {
    if !options.is_null() {
        unsafe {
            let _ = Box::from_raw(options);
        }
    }
}

// Parse SVG from data
#[no_mangle]
pub extern "C" fn resvg_parse_tree_from_data(
    data: *const c_char,
    length: c_uint,
    options: *const ResvgOptions,
) -> *mut ResvgRenderTree {
    if data.is_null() || options.is_null() {
        return ptr::null_mut();
    }

    unsafe {
        let svg_data = slice::from_raw_parts(data as *const u8, length as usize);
        let svg_str = match std::str::from_utf8(svg_data) {
            Ok(s) => s,
            Err(_) => return ptr::null_mut(),
        };

        let opts = &(*options).usvg_options;

        match Tree::from_str(svg_str, opts) {
            Ok(tree) => Box::into_raw(Box::new(ResvgRenderTree { tree })),
            Err(_) => ptr::null_mut(),
        }
    }
}



// Get image size
#[no_mangle]
pub extern "C" fn resvg_get_image_size(tree: *const ResvgRenderTree) -> ResvgSize {
    if tree.is_null() {
        return ResvgSize { width: 0.0, height: 0.0 };
    }

    unsafe {
        let size = (*tree).tree.size();
        ResvgSize {
            width: size.width(),
            height: size.height(),
        }
    }
}

// Identity transform
#[no_mangle]
pub extern "C" fn resvg_transform_identity() -> ResvgTransform {
    ResvgTransform {
        a: 1.0,
        b: 0.0,
        c: 0.0,
        d: 1.0,
        e: 0.0,
        f: 0.0,
    }
}

// Render
#[no_mangle]
pub extern "C" fn resvg_render(
    tree: *const ResvgRenderTree,
    fit_to: ResvgFitTo,
    transform: ResvgTransform,
    width: c_uint,
    height: c_uint,
    pixels: *mut c_char,
) {
    if tree.is_null() || pixels.is_null() {
        return;
    }

    unsafe {
        let mut pixmap = match Pixmap::new(width, height) {
            Some(p) => p,
            None => return,
        };

        let ts = tiny_skia::Transform::from_row(
            transform.a,
            transform.b,
            transform.c,
            transform.d,
            transform.e,
            transform.f,
        );

        let scale = match fit_to.fit_type {
            ResvgFitToType::Zoom => fit_to.value,
            _ => 1.0,
        };

        let final_transform = ts.pre_scale(scale, scale);

        resvg::render(&(*tree).tree, final_transform, &mut pixmap.as_mut());

        let pixel_data = pixmap.data();
        // TODO : Attention : tiny-skia (utilisé par resvg) produit des pixels au format RGBA pré-multiplié. Si ton moteur de rendu (Vulkan/ImGui) attend du RGBA classique, tes icônes avec de la transparence auront un rendu bizarre (trop sombres ou bords noirs).
        let output = slice::from_raw_parts_mut(pixels as *mut u8, (width * height * 4) as usize);
        output.copy_from_slice(pixel_data);
    }
}

// Destroy tree
#[no_mangle]
pub extern "C" fn resvg_tree_destroy(tree: *mut ResvgRenderTree) {
    if !tree.is_null() {
        unsafe {
            let _ = Box::from_raw(tree);
        }
    }
}

// Initialize library (no-op, just for compatibility)
#[no_mangle]
pub extern "C" fn resvg_init() {
    // No-op
}