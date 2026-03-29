#pragma once
#include <stddef.h>
#include <stdio.h>

typedef enum {
    RB_COLOR_BLACK,
    RB_COLOR_RED,
} rb_color_t;

typedef enum {
    RB_DIRECTION_LEFT,
    RB_DIRECTION_RIGHT,
} rb_direction_t;

typedef struct rb_node {
    rb_color_t color;
    struct rb_node* parent;
    struct rb_node* left;
    struct rb_node* right;
} rb_node_t;

typedef struct {
    size_t (*value_of_node)(rb_node_t* node);
    size_t (*length_of_node)(rb_node_t* node);
    rb_node_t* root;
} rb_tree_t;

void rb_insert(rb_tree_t* tree, rb_node_t* node);
void rb_remove(rb_tree_t* tree, rb_node_t* node);

typedef enum {
    RB_SEARCH_TYPE_EXACT, /* Find an exact match */
    RB_SEARCH_TYPE_NEAREST, /* Find the nearest match */
    RB_SEARCH_TYPE_NEAREST_LT, /* Find the nearest match that is less than the search value */
    RB_SEARCH_TYPE_NEAREST_LTE, /* Find the nearest match that is less than or equals to the search value */
    RB_SEARCH_TYPE_NEAREST_GT, /* Find the nearest match that is greater than the search value */
    RB_SEARCH_TYPE_NEAREST_GTE, /* Find the nearest match that is greater than or equals to the search value */
} rb_search_type_t;

rb_node_t* rb_find_first(rb_tree_t* tree);
rb_node_t* rb_find(rb_tree_t* tree, size_t needle, rb_search_type_t search_type);
rb_node_t* rb_find_within(rb_tree_t* tree, size_t needle);

size_t rb_find_first_gap(rb_tree_t* tree, size_t start, size_t end, size_t size);
