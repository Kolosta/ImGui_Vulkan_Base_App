// The single translation unit that compiles the Vulkan Memory Allocator. No other
// TU may define VMA_IMPLEMENTATION (ODR). Vulkan must be included before VMA.
#include <vulkan/vulkan.h>

// Silence VMA's known benign warnings under -Wall (GCC/Clang) so the build stays
// clean without weakening warnings for our own code.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic ignored "-Wunused-variable"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wnullability-completeness"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
