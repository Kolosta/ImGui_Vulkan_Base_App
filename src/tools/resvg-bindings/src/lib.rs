// // // use std::os::raw::{c_char, c_uint, c_float};
// // // use std::ptr;
// // // use std::slice;

// // // use usvg::{Options, Tree};
// // // use tiny_skia::Pixmap;

// // // // Opaque types
// // // pub struct ResvgOptions {
// // //     usvg_options: Options<'static>,
// // // }

// // // pub struct ResvgRenderTree {
// // //     tree: Tree,
// // // }

// // // #[repr(C)]
// // // pub struct ResvgSize {
// // //     pub width: c_float,
// // //     pub height: c_float,
// // // }

// // // #[repr(C)]
// // // pub struct ResvgTransform {
// // //     pub a: c_float,
// // //     pub b: c_float,
// // //     pub c: c_float,
// // //     pub d: c_float,
// // //     pub e: c_float,
// // //     pub f: c_float,
// // // }

// // // #[repr(C)]
// // // pub enum ResvgFitToType {
// // //     Original,
// // //     Width,
// // //     Height,
// // //     Zoom,
// // // }

// // // #[repr(C)]
// // // pub struct ResvgFitTo {
// // //     pub fit_type: ResvgFitToType,
// // //     pub value: c_float,
// // // }

// // // // Create options
// // // #[no_mangle]
// // // pub extern "C" fn resvg_options_create() -> *mut ResvgOptions {
// // //     let options = Box::new(ResvgOptions {
// // //         usvg_options: Options::default(),
// // //     });
// // //     Box::into_raw(options)
// // // }

// // // // Load system fonts (no-op in C API, fonts are loaded by default)
// // // #[no_mangle]
// // // pub extern "C" fn resvg_options_load_system_fonts(_options: *mut ResvgOptions) {
// // //     // No-op: usvg loads system fonts by default
// // // }

// // // // Destroy options
// // // #[no_mangle]
// // // pub extern "C" fn resvg_options_destroy(options: *mut ResvgOptions) {
// // //     if !options.is_null() {
// // //         unsafe {
// // //             let _ = Box::from_raw(options);
// // //         }
// // //     }
// // // }

// // // // Parse SVG from data
// // // #[no_mangle]
// // // pub extern "C" fn resvg_parse_tree_from_data(
// // //     data: *const c_char,
// // //     length: c_uint,
// // //     options: *const ResvgOptions,
// // // ) -> *mut ResvgRenderTree {
// // //     if data.is_null() || options.is_null() {
// // //         return ptr::null_mut();
// // //     }

// // //     unsafe {
// // //         let svg_data = slice::from_raw_parts(data as *const u8, length as usize);
// // //         let svg_str = match std::str::from_utf8(svg_data) {
// // //             Ok(s) => s,
// // //             Err(_) => return ptr::null_mut(),
// // //         };

// // //         let opts = &(*options).usvg_options;

// // //         match Tree::from_str(svg_str, opts) {
// // //             Ok(tree) => Box::into_raw(Box::new(ResvgRenderTree { tree })),
// // //             Err(_) => ptr::null_mut(),
// // //         }
// // //     }
// // // }

// // // // Get image size
// // // #[no_mangle]
// // // pub extern "C" fn resvg_get_image_size(tree: *const ResvgRenderTree) -> ResvgSize {
// // //     if tree.is_null() {
// // //         return ResvgSize { width: 0.0, height: 0.0 };
// // //     }

// // //     unsafe {
// // //         let size = (*tree).tree.size();
// // //         ResvgSize {
// // //             width: size.width(),
// // //             height: size.height(),
// // //         }
// // //     }
// // // }

// // // // Identity transform
// // // #[no_mangle]
// // // pub extern "C" fn resvg_transform_identity() -> ResvgTransform {
// // //     ResvgTransform {
// // //         a: 1.0,
// // //         b: 0.0,
// // //         c: 0.0,
// // //         d: 1.0,
// // //         e: 0.0,
// // //         f: 0.0,
// // //     }
// // // }

// // // // Render
// // // #[no_mangle]
// // // pub extern "C" fn resvg_render(
// // //     tree: *const ResvgRenderTree,
// // //     fit_to: ResvgFitTo,
// // //     transform: ResvgTransform,
// // //     width: c_uint,
// // //     height: c_uint,
// // //     pixels: *mut c_char,
// // // ) {
// // //     if tree.is_null() || pixels.is_null() {
// // //         return;
// // //     }

// // //     unsafe {
// // //         let mut pixmap = match Pixmap::new(width, height) {
// // //             Some(p) => p,
// // //             None => return,
// // //         };

// // //         let ts = tiny_skia::Transform::from_row(
// // //             transform.a,
// // //             transform.b,
// // //             transform.c,
// // //             transform.d,
// // //             transform.e,
// // //             transform.f,
// // //         );

// // //         let scale = match fit_to.fit_type {
// // //             ResvgFitToType::Zoom => fit_to.value,
// // //             _ => 1.0,
// // //         };

// // //         let final_transform = ts.pre_scale(scale, scale);

// // //         resvg::render(&(*tree).tree, final_transform, &mut pixmap.as_mut());

// // //         let pixel_data = pixmap.data();
// // //         let output = slice::from_raw_parts_mut(pixels as *mut u8, (width * height * 4) as usize);
// // //         output.copy_from_slice(pixel_data);
// // //     }
// // // }

// // // // Destroy tree
// // // #[no_mangle]
// // // pub extern "C" fn resvg_tree_destroy(tree: *mut ResvgRenderTree) {
// // //     if !tree.is_null() {
// // //         unsafe {
// // //             let _ = Box::from_raw(tree);
// // //         }
// // //     }
// // // }

// // // // Initialize library (no-op, just for compatibility)
// // // #[no_mangle]
// // // pub extern "C" fn resvg_init() {
// // //     // No-op
// // // }






// // use std::ffi::{CStr, CString};
// // use std::os::raw::{c_char, c_void};
// // use std::ptr;
// // use std::slice;

// // pub mod color_parser;

// // // Re-export resvg types
// // pub use resvg::usvg;
// // pub use resvg::tiny_skia;

// // #[repr(C)]
// // pub struct resvg_options {
// //     _private: [u8; 0],
// // }

// // #[repr(C)]
// // pub struct resvg_render_tree {
// //     tree: usvg::Tree,
// // }

// // #[repr(C)]
// // pub struct resvg_size {
// //     pub width: f32,
// //     pub height: f32,
// // }

// // #[repr(C)]
// // pub struct resvg_transform {
// //     pub a: f32,
// //     pub b: f32,
// //     pub c: f32,
// //     pub d: f32,
// //     pub e: f32,
// //     pub f: f32,
// // }

// // #[repr(C)]
// // pub enum resvg_fit_to_type {
// //     RESVG_FIT_TO_TYPE_ORIGINAL,
// //     RESVG_FIT_TO_TYPE_WIDTH,
// //     RESVG_FIT_TO_TYPE_HEIGHT,
// //     RESVG_FIT_TO_TYPE_ZOOM,
// // }

// // #[repr(C)]
// // pub struct resvg_fit_to {
// //     pub type_: resvg_fit_to_type,
// //     pub value: f32,
// // }

// // // Initialize logging (no-op for now)
// // #[no_mangle]
// // pub extern "C" fn resvg_init_log() {
// //     // Could initialize logging here if needed
// // }

// // // Create default options
// // #[no_mangle]
// // pub extern "C" fn resvg_options_create() -> *mut resvg_options {
// //     // Return a dummy pointer since we're using default options
// //     Box::into_raw(Box::new([0u8; 1])) as *mut resvg_options
// // }

// // // Load system fonts (no-op in this simple implementation)
// // #[no_mangle]
// // pub extern "C" fn resvg_options_load_system_fonts(_opt: *mut resvg_options) {
// //     // Font loading would go here
// // }

// // // Destroy options
// // #[no_mangle]
// // pub extern "C" fn resvg_options_destroy(opt: *mut resvg_options) {
// //     if !opt.is_null() {
// //         unsafe {
// //             let _ = Box::from_raw(opt as *mut u8);
// //         }
// //     }
// // }

// // // Parse SVG from data
// // #[no_mangle]
// // pub extern "C" fn resvg_parse_tree_from_data(
// //     data: *const c_char,
// //     len: usize,
// //     _opt: *const resvg_options,
// // ) -> *mut resvg_render_tree {
// //     if data.is_null() {
// //         return ptr::null_mut();
// //     }

// //     let svg_data = unsafe { slice::from_raw_parts(data as *const u8, len) };
// //     let svg_str = match std::str::from_utf8(svg_data) {
// //         Ok(s) => s,
// //         Err(_) => return ptr::null_mut(),
// //     };

// //     // Parse with default options and DTD support
// //     let opt = usvg::Options {
// //         ..Default::default()
// //     };

// //     match usvg::Tree::from_str(svg_str, &opt) {
// //         Ok(tree) => {
// //             let render_tree = Box::new(resvg_render_tree { tree });
// //             Box::into_raw(render_tree)
// //         }
// //         Err(_) => ptr::null_mut(),
// //     }
// // }

// // // Parse SVG from file
// // #[no_mangle]
// // pub extern "C" fn resvg_parse_tree_from_file(
// //     path: *const c_char,
// //     _opt: *const resvg_options,
// // ) -> *mut resvg_render_tree {
// //     if path.is_null() {
// //         return ptr::null_mut();
// //     }

// //     let c_str = unsafe { CStr::from_ptr(path) };
// //     let path_str = match c_str.to_str() {
// //         Ok(s) => s,
// //         Err(_) => return ptr::null_mut(),
// //     };

// //     let svg_data = match std::fs::read_to_string(path_str) {
// //         Ok(data) => data,
// //         Err(_) => return ptr::null_mut(),
// //     };

// //     let opt = usvg::Options {
// //         ..Default::default()
// //     };

// //     match usvg::Tree::from_str(&svg_data, &opt) {
// //         Ok(tree) => {
// //             let render_tree = Box::new(resvg_render_tree { tree });
// //             Box::into_raw(render_tree)
// //         }
// //         Err(_) => ptr::null_mut(),
// //     }
// // }

// // // Get image size from tree
// // #[no_mangle]
// // pub extern "C" fn resvg_get_image_size(tree: *const resvg_render_tree) -> resvg_size {
// //     if tree.is_null() {
// //         return resvg_size {
// //             width: 0.0,
// //             height: 0.0,
// //         };
// //     }

// //     let tree = unsafe { &(*tree).tree };
// //     let size = tree.size();

// //     resvg_size {
// //         width: size.width(),
// //         height: size.height(),
// //     }
// // }

// // // Identity transform
// // #[no_mangle]
// // pub extern "C" fn resvg_transform_identity() -> resvg_transform {
// //     resvg_transform {
// //         a: 1.0,
// //         b: 0.0,
// //         c: 0.0,
// //         d: 1.0,
// //         e: 0.0,
// //         f: 0.0,
// //     }
// // }

// // // Render tree to pixmap
// // #[no_mangle]
// // pub extern "C" fn resvg_render(
// //     tree: *const resvg_render_tree,
// //     fit_to: resvg_fit_to,
// //     transform: resvg_transform,
// //     width: u32,
// //     height: u32,
// //     pixmap: *mut u8,
// // ) {
// //     if tree.is_null() || pixmap.is_null() {
// //         return;
// //     }

// //     let tree = unsafe { &(*tree).tree };

// //     // Create pixmap
// //     let mut tiny_pixmap = match tiny_skia::Pixmap::new(width, height) {
// //         Some(p) => p,
// //         None => return,
// //     };

// //     // Calculate transform based on fit_to
// //     let ts = tiny_skia::Transform::from_row(
// //         transform.a,
// //         transform.b,
// //         transform.c,
// //         transform.d,
// //         transform.e,
// //         transform.f,
// //     );

// //     let final_transform = match fit_to.type_ {
// //         resvg_fit_to_type::RESVG_FIT_TO_TYPE_ZOOM => {
// //             let scale = fit_to.value;
// //             ts.pre_scale(scale, scale)
// //         }
// //         resvg_fit_to_type::RESVG_FIT_TO_TYPE_WIDTH => {
// //             let scale = width as f32 / tree.size().width();
// //             ts.pre_scale(scale, scale)
// //         }
// //         resvg_fit_to_type::RESVG_FIT_TO_TYPE_HEIGHT => {
// //             let scale = height as f32 / tree.size().height();
// //             ts.pre_scale(scale, scale)
// //         }
// //         resvg_fit_to_type::RESVG_FIT_TO_TYPE_ORIGINAL => ts,
// //     };

// //     // Render
// //     resvg::render(tree, final_transform, &mut tiny_pixmap.as_mut());

// //     // Copy pixmap data to output buffer
// //     let pixmap_data = tiny_pixmap.data();
// //     unsafe {
// //         ptr::copy_nonoverlapping(
// //             pixmap_data.as_ptr(),
// //             pixmap,
// //             (width * height * 4) as usize,
// //         );
// //     }
// // }

// // // Destroy tree
// // #[no_mangle]
// // pub extern "C" fn resvg_tree_destroy(tree: *mut resvg_render_tree) {
// //     if !tree.is_null() {
// //         unsafe {
// //             let _ = Box::from_raw(tree);
// //         }
// //     }
// // }

// // // Check if tree is valid
// // #[no_mangle]
// // pub extern "C" fn resvg_is_image_empty(tree: *const resvg_render_tree) -> bool {
// //     if tree.is_null() {
// //         return true;
// //     }

// //     let tree = unsafe { &(*tree).tree };
// //     tree.root().children().count() == 0
// // }

// // // Get viewbox
// // #[no_mangle]
// // pub extern "C" fn resvg_get_image_viewbox(tree: *const resvg_render_tree) -> resvg_transform {
// //     if tree.is_null() {
// //         return resvg_transform_identity();
// //     }

// //     let tree = unsafe { &(*tree).tree };
// //     let vb = tree.view_box();

// //     resvg_transform {
// //         a: vb.rect.width(),
// //         b: 0.0,
// //         c: 0.0,
// //         d: vb.rect.height(),
// //         e: vb.rect.x(),
// //         f: vb.rect.y(),
// //     }
// // }







// use std::ffi::CStr;
// use std::os::raw::c_char;
// use std::ptr;
// use std::slice;

// pub mod color_parser;

// // Re-export resvg types
// pub use resvg::usvg;
// pub use resvg::tiny_skia;

// #[repr(C)]
// pub struct resvg_options {
//     _private: [u8; 0],
// }

// #[repr(C)]
// pub struct resvg_render_tree {
//     tree: usvg::Tree,
// }

// #[repr(C)]
// pub struct resvg_size {
//     pub width: f32,
//     pub height: f32,
// }

// #[repr(C)]
// pub struct resvg_transform {
//     pub a: f32,
//     pub b: f32,
//     pub c: f32,
//     pub d: f32,
//     pub e: f32,
//     pub f: f32,
// }

// #[repr(C)]
// #[allow(non_camel_case_types)]
// pub enum resvg_fit_to_type {
//     RESVG_FIT_TO_TYPE_ORIGINAL,
//     RESVG_FIT_TO_TYPE_WIDTH,
//     RESVG_FIT_TO_TYPE_HEIGHT,
//     RESVG_FIT_TO_TYPE_ZOOM,
// }

// #[repr(C)]
// pub struct resvg_fit_to {
//     pub type_: resvg_fit_to_type,
//     pub value: f32,
// }

// // Initialize logging (no-op for now)
// #[no_mangle]
// pub extern "C" fn resvg_init_log() {
//     // Could initialize logging here if needed
// }

// // Create default options
// #[no_mangle]
// pub extern "C" fn resvg_options_create() -> *mut resvg_options {
//     // Return a dummy pointer since we're using default options
//     Box::into_raw(Box::new([0u8; 1])) as *mut resvg_options
// }

// // Load system fonts (no-op in this simple implementation)
// #[no_mangle]
// pub extern "C" fn resvg_options_load_system_fonts(_opt: *mut resvg_options) {
//     // Font loading would go here
// }

// // Destroy options
// #[no_mangle]
// pub extern "C" fn resvg_options_destroy(opt: *mut resvg_options) {
//     if !opt.is_null() {
//         unsafe {
//             let _ = Box::from_raw(opt as *mut u8);
//         }
//     }
// }

// // Parse SVG from data
// #[no_mangle]
// pub extern "C" fn resvg_parse_tree_from_data(
//     data: *const c_char,
//     len: usize,
//     _opt: *const resvg_options,
// ) -> *mut resvg_render_tree {
//     if data.is_null() {
//         return ptr::null_mut();
//     }

//     let svg_data = unsafe { slice::from_raw_parts(data as *const u8, len) };
//     let svg_str = match std::str::from_utf8(svg_data) {
//         Ok(s) => s,
//         Err(_) => return ptr::null_mut(),
//     };

//     // Parse with default options and DTD support
//     let opt = usvg::Options {
//         ..Default::default()
//     };

//     match usvg::Tree::from_str(svg_str, &opt) {
//         Ok(tree) => {
//             let render_tree = Box::new(resvg_render_tree { tree });
//             Box::into_raw(render_tree)
//         }
//         Err(_) => ptr::null_mut(),
//     }
// }

// // Parse SVG from file
// #[no_mangle]
// pub extern "C" fn resvg_parse_tree_from_file(
//     path: *const c_char,
//     _opt: *const resvg_options,
// ) -> *mut resvg_render_tree {
//     if path.is_null() {
//         return ptr::null_mut();
//     }

//     let c_str = unsafe { CStr::from_ptr(path) };
//     let path_str = match c_str.to_str() {
//         Ok(s) => s,
//         Err(_) => return ptr::null_mut(),
//     };

//     let svg_data = match std::fs::read_to_string(path_str) {
//         Ok(data) => data,
//         Err(_) => return ptr::null_mut(),
//     };

//     let opt = usvg::Options {
//         ..Default::default()
//     };

//     match usvg::Tree::from_str(&svg_data, &opt) {
//         Ok(tree) => {
//             let render_tree = Box::new(resvg_render_tree { tree });
//             Box::into_raw(render_tree)
//         }
//         Err(_) => ptr::null_mut(),
//     }
// }

// // Get image size from tree
// #[no_mangle]
// pub extern "C" fn resvg_get_image_size(tree: *const resvg_render_tree) -> resvg_size {
//     if tree.is_null() {
//         return resvg_size {
//             width: 0.0,
//             height: 0.0,
//         };
//     }

//     let tree = unsafe { &(*tree).tree };
//     let size = tree.size();

//     resvg_size {
//         width: size.width(),
//         height: size.height(),
//     }
// }

// // Identity transform
// #[no_mangle]
// pub extern "C" fn resvg_transform_identity() -> resvg_transform {
//     resvg_transform {
//         a: 1.0,
//         b: 0.0,
//         c: 0.0,
//         d: 1.0,
//         e: 0.0,
//         f: 0.0,
//     }
// }

// // Render tree to pixmap
// #[no_mangle]
// pub extern "C" fn resvg_render(
//     tree: *const resvg_render_tree,
//     fit_to: resvg_fit_to,
//     transform: resvg_transform,
//     width: u32,
//     height: u32,
//     pixmap: *mut u8,
// ) {
//     if tree.is_null() || pixmap.is_null() {
//         return;
//     }

//     let tree = unsafe { &(*tree).tree };

//     // Create pixmap
//     let mut tiny_pixmap = match tiny_skia::Pixmap::new(width, height) {
//         Some(p) => p,
//         None => return,
//     };

//     // Calculate transform based on fit_to
//     let ts = tiny_skia::Transform::from_row(
//         transform.a,
//         transform.b,
//         transform.c,
//         transform.d,
//         transform.e,
//         transform.f,
//     );

//     let final_transform = match fit_to.type_ {
//         resvg_fit_to_type::RESVG_FIT_TO_TYPE_ZOOM => {
//             let scale = fit_to.value;
//             ts.pre_scale(scale, scale)
//         }
//         resvg_fit_to_type::RESVG_FIT_TO_TYPE_WIDTH => {
//             let scale = width as f32 / tree.size().width();
//             ts.pre_scale(scale, scale)
//         }
//         resvg_fit_to_type::RESVG_FIT_TO_TYPE_HEIGHT => {
//             let scale = height as f32 / tree.size().height();
//             ts.pre_scale(scale, scale)
//         }
//         resvg_fit_to_type::RESVG_FIT_TO_TYPE_ORIGINAL => ts,
//     };

//     // Render
//     resvg::render(tree, final_transform, &mut tiny_pixmap.as_mut());

//     // Copy pixmap data to output buffer
//     let pixmap_data = tiny_pixmap.data();
//     unsafe {
//         ptr::copy_nonoverlapping(
//             pixmap_data.as_ptr(),
//             pixmap,
//             (width * height * 4) as usize,
//         );
//     }
// }

// // Destroy tree
// #[no_mangle]
// pub extern "C" fn resvg_tree_destroy(tree: *mut resvg_render_tree) {
//     if !tree.is_null() {
//         unsafe {
//             let _ = Box::from_raw(tree);
//         }
//     }
// }

// // Check if tree is valid
// #[no_mangle]
// pub extern "C" fn resvg_is_image_empty(tree: *const resvg_render_tree) -> bool {
//     if tree.is_null() {
//         return true;
//     }

//     let tree = unsafe { &(*tree).tree };
//     tree.root().children().is_empty()
// }

// // Get viewbox
// #[no_mangle]
// pub extern "C" fn resvg_get_image_viewbox(tree: *const resvg_render_tree) -> resvg_transform {
//     if tree.is_null() {
//         return resvg_transform_identity();
//     }

//     let tree = unsafe { &(*tree).tree };
//     let size = tree.size();

//     resvg_transform {
//         a: size.width(),
//         b: 0.0,
//         c: 0.0,
//         d: size.height(),
//         e: 0.0,
//         f: 0.0,
//     }
// }



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
