#ifndef CONFIG
#define CONFIG
#include "size_classes.hpp"

static constexpr uint32_t  PAGE_SIZE = 8000; // 8 KB page size

static constexpr uint32_t MAX_PAGES = 255;

// static constexpr size_t MMAP_THRESHOLD = 256000;
static constexpr uint32_t  MMAP_THRESHOLD = 400;

static constexpr SizeClassInfo SizeClasses[] = {
  {     0,    0,    0},  
  {     8,    1,   32}, 
  {    16,    1,   32}, 
  {    32,    1,   32}, 
  {    64,    1,   32}, 
  {    80,    1,   32}, 
  {    96,    1,   32}, 
  {   112,    1,   32}, 
  {   128,    1,   32}, 
  {   160,    1,   32}, 
  {   176,    1,   32}, 
  {   208,    1,   32}, 
  {   256,    1,   32}, 
  {   312,    1,   32}, 
  {   384,    1,   32}, 
  {   448,    1,   32}, 
  {   512,    1,   32}, 
  {   576,    1,   32}, 
  {   704,    1,   32}, 
  {   896,    1,   32}, 
  {  1024,    1,   32}, 
  {  1152,    2,   32}, 
  {  1408,    2,   32}, 
  {  1792,    2,   32}, 
  {  2048,    2,   32}, 
  {  2688,    2,   24}, 
  {  3456,    3,   18}, 
  {  4096,    1,   16}, 
  {  4736,    3,   13}, 
  {  6144,    3,   10}, 
  {  8192,    1,    8}, 
  {  9472,    5,    6}, 
  { 12288,    3,    5}, 
  { 16384,    2,    4}, 
  { 20480,    5,    3}, 
  { 28672,    7,    2}, 
  { 32768,    4,    2}, 
  { 40960,    5,    2}, 
  { 49152,    6,    2}, 
  { 65536,    8,    2}, 
  { 73728,    9,    2}, 
  { 81920,   10,    2}, 
  { 98304,   12,    2}, 
  {114688,   14,    2}, 
  {131072,   16,    2}, 
  {155648,   19,    2}, 
  {204800,   25,    2}, 
  {262144,   32,    2}
};

static constexpr uint32_t NUM_SIZE_CLASSES = sizeof(SizeClasses) / sizeof(SizeClasses[0]);

#endif