#ifndef RESVG_C_H
#define RESVG_C_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum ResvgFitToType {
  Original,
  Width,
  Height,
  Zoom,
} ResvgFitToType;

typedef struct ResvgOptions ResvgOptions;

typedef struct ResvgRenderTree ResvgRenderTree;

typedef struct ResvgSize {
  float width;
  float height;
} ResvgSize;

typedef struct ResvgTransform {
  float a;
  float b;
  float c;
  float d;
  float e;
  float f;
} ResvgTransform;

typedef struct ResvgFitTo {
  enum ResvgFitToType fit_type;
  float value;
} ResvgFitTo;

struct ResvgOptions *resvg_options_create(void);

void resvg_options_load_system_fonts(struct ResvgOptions *_options);

void resvg_options_destroy(struct ResvgOptions *options);

struct ResvgRenderTree *resvg_parse_tree_from_data(const char *data,
                                                   unsigned int length,
                                                   const struct ResvgOptions *options);

struct ResvgSize resvg_get_image_size(const struct ResvgRenderTree *tree);

struct ResvgTransform resvg_transform_identity(void);

void resvg_render(const struct ResvgRenderTree *tree,
                  struct ResvgFitTo fit_to,
                  struct ResvgTransform transform,
                  unsigned int width,
                  unsigned int height,
                  char *pixels);

void resvg_tree_destroy(struct ResvgRenderTree *tree);

void resvg_init(void);

#endif /* RESVG_C_H */
