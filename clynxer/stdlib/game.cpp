// Clynxer `game` module: a thin C++ shim over the Rust + macroquad backend in
// `rust/game`. Every drawing/input/sprite op is a `cdecl:<ret>(...)` function
// whose symbol is exported by the linked Rust static library; this file only
// owns the callback glue and the registration table.
//
// The Rust loop (`lynxer_game_run`) owns the window and calls
// `clynxer_game_frame` once per frame, which invokes the registered Lynxer
// `update(dt)` and `draw()` callbacks through the host API.

#include "lynxer_native_abi.h"

#include <cstdint>
#include <string>

namespace {

// Host services supplied by the interpreter in `lynxer_module_attach_v1`. The
// interpreter's struct is a stack local, so it is stored by value.
LynxerHostApi g_host{};
bool g_attached = false;
bool g_callbackFailed = false;
std::string g_drawCallback;
std::string g_updateCallback;

// Provided by the Rust static library.
extern "C" int lynxer_game_run(void (*frame)(void*, double), void* user,
                               int (*quit)());

extern "C" void clynxer_game_frame(void* user, double dt) {
    (void)user;
    if (!g_attached || g_host.invoke == nullptr) {
        return;
    }
    if (!g_updateCallback.empty()) {
        if (g_host.invoke(g_host.context, g_updateCallback.c_str(), 1, dt) != 0) {
            g_callbackFailed = true;
        }
    }
    if (!g_drawCallback.empty()) {
        if (g_host.invoke(g_host.context, g_drawCallback.c_str(), 0, 0.0) != 0) {
            g_callbackFailed = true;
        }
    }
}

extern "C" int clynxer_game_should_quit() {
    // Stop the loop on Ctrl-C or when a callback failed, so the pending error
    // can be rethrown by the interpreter when `run` returns.
    if (g_callbackFailed) {
        return 1;
    }
    if (!g_attached || g_host.interrupted == nullptr) {
        return 0;
    }
    return g_host.interrupted(g_host.context);
}

// Packed arguments arrive as four scalars (see lynxer_native_abi.h).
std::string firstArgument(const double*, std::int64_t,
                          const char* const* strs, std::int64_t str_count) {
    if (strs == nullptr || str_count < 1 || strs[0] == nullptr) {
        return "";
    }
    return strs[0];
}

extern "C" std::int64_t game_run(const double*, std::int64_t,
                                 const char* const*, std::int64_t) {
    if (!g_attached) {
        return -1;
    }
    return lynxer_game_run(clynxer_game_frame, nullptr, clynxer_game_should_quit);
}

extern "C" std::int64_t game_set_draw_callback(const double* nums,
                                               std::int64_t num_count,
                                               const char* const* strs,
                                               std::int64_t str_count) {
    g_drawCallback = firstArgument(nums, num_count, strs, str_count);
    return 0;
}

extern "C" std::int64_t game_set_update_callback(const double* nums,
                                                 std::int64_t num_count,
                                                 const char* const* strs,
                                                 std::int64_t str_count) {
    g_updateCallback = firstArgument(nums, num_count, strs, str_count);
    return 0;
}

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

struct Entry {
    const char* name;
    const char* symbol;
    const char* signature;
};

#define INT_OP(name, symbol) {name, symbol, "cdecl:int64(...)"}
#define FLOAT_OP(name, symbol) {name, symbol, "cdecl:float64(...)"}
#define STRING_OP(name, symbol) {name, symbol, "cdecl:cstring(...)"}

const Entry kEntries[] = {
    // Window and lifecycle.
    INT_OP("init", "lynxer_game_init"),
    INT_OP("setTitle", "lynxer_game_set_title"),
    INT_OP("setBackground", "lynxer_game_set_background"),
    INT_OP("getWidth", "lynxer_game_get_width"),
    INT_OP("getHeight", "lynxer_game_get_height"),
    INT_OP("setWindowSize", "lynxer_game_set_window_size"),
    INT_OP("setResizable", "lynxer_game_set_resizable"),
    INT_OP("setFullscreen", "lynxer_game_set_fullscreen"),
    INT_OP("setMouseVisible", "lynxer_game_set_mouse_visible"),
    INT_OP("setFPSCap", "lynxer_game_set_fps_cap"),
    INT_OP("getFPS", "lynxer_game_get_fps"),
    INT_OP("close", "lynxer_game_close"),
    INT_OP("isOpen", "lynxer_game_is_open"),
    INT_OP("run", "game_run"),
    INT_OP("setDrawCallback", "game_set_draw_callback"),
    INT_OP("setUpdateCallback", "game_set_update_callback"),
    FLOAT_OP("deltaTime", "lynxer_game_delta_time"),
    FLOAT_OP("getTime", "lynxer_game_get_time"),

    // Draw loop and shapes.
    INT_OP("beginDraw", "lynxer_game_begin_draw"),
    INT_OP("endDraw", "lynxer_game_end_draw"),
    INT_OP("drawRect", "lynxer_game_draw_rect"),
    INT_OP("drawRectOutline", "lynxer_game_draw_rect_outline"),
    INT_OP("drawCircle", "lynxer_game_draw_circle"),
    INT_OP("drawCircleOutline", "lynxer_game_draw_circle_outline"),
    INT_OP("drawEllipse", "lynxer_game_draw_ellipse"),
    INT_OP("drawEllipseOutline", "lynxer_game_draw_ellipse_outline"),
    INT_OP("drawLine", "lynxer_game_draw_line"),
    INT_OP("drawTriangle", "lynxer_game_draw_triangle"),
    INT_OP("drawTriangleOutline", "lynxer_game_draw_triangle_outline"),
    INT_OP("drawPoint", "lynxer_game_draw_point"),
    INT_OP("drawRectRoundedFilled", "lynxer_game_draw_rect_rounded_filled"),
    INT_OP("drawRectRoundedOutline", "lynxer_game_draw_rect_rounded_outline"),
    INT_OP("drawStar", "lynxer_game_draw_star"),
    INT_OP("drawDashedLine", "lynxer_game_draw_dashed_line"),
    INT_OP("drawCross", "lynxer_game_draw_cross"),
    INT_OP("drawGradientRect", "lynxer_game_draw_gradient_rect"),
    INT_OP("drawArc", "lynxer_game_draw_arc"),
    INT_OP("drawArcFilled", "lynxer_game_draw_arc_filled"),
    INT_OP("drawPolygon", "lynxer_game_draw_polygon"),
    INT_OP("drawPolygonOutline", "lynxer_game_draw_polygon_outline"),
    INT_OP("drawPolyline", "lynxer_game_draw_polyline"),
    INT_OP("drawPoints", "lynxer_game_draw_points"),
    INT_OP("drawLines", "lynxer_game_draw_lines"),

    // Text.
    INT_OP("drawText", "lynxer_game_draw_text"),
    INT_OP("drawTextStyled", "lynxer_game_draw_text_styled"),
    INT_OP("drawTextAnchored", "lynxer_game_draw_text_anchored"),

    // Input.
    INT_OP("keyDown", "lynxer_game_key_down"),
    INT_OP("keyUp", "lynxer_game_key_up"),
    INT_OP("keyPressed", "lynxer_game_key_pressed"),
    INT_OP("keyReleased", "lynxer_game_key_released"),
    INT_OP("keyCode", "lynxer_game_key_code"),
    INT_OP("mouseLeft", "lynxer_game_mouse_left"),
    INT_OP("mouseRight", "lynxer_game_mouse_right"),
    INT_OP("mouseMiddle", "lynxer_game_mouse_middle"),
    INT_OP("mouseButtonDown", "lynxer_game_mouse_button_down"),
    INT_OP("mouseButtonPressed", "lynxer_game_mouse_button_pressed"),
    INT_OP("mouseButtonReleased", "lynxer_game_mouse_button_released"),
    INT_OP("mouseButtonCode", "lynxer_game_mouse_button_code"),
    FLOAT_OP("mouseX", "lynxer_game_mouse_x"),
    FLOAT_OP("mouseY", "lynxer_game_mouse_y"),
    FLOAT_OP("mouseDeltaX", "lynxer_game_mouse_delta_x"),
    FLOAT_OP("mouseDeltaY", "lynxer_game_mouse_delta_y"),
    FLOAT_OP("mouseScrollX", "lynxer_game_mouse_scroll_x"),
    FLOAT_OP("mouseScrollY", "lynxer_game_mouse_scroll_y"),

    // Sprites.
    INT_OP("makeSolidSprite", "lynxer_game_make_solid_sprite"),
    INT_OP("loadSprite", "lynxer_game_load_sprite"),
    INT_OP("loadTexture", "lynxer_game_load_texture"),
    INT_OP("setSpriteTexture", "lynxer_game_set_sprite_texture"),
    INT_OP("getSpriteAlpha", "lynxer_game_get_sprite_alpha"),
    INT_OP("getSpriteVisible", "lynxer_game_get_sprite_visible"),
    INT_OP("spriteExists", "lynxer_game_sprite_exists"),
    INT_OP("setSpritePos", "lynxer_game_set_sprite_pos"),
    INT_OP("setSpritePosition", "lynxer_game_set_sprite_pos"),
    INT_OP("setSpriteAngle", "lynxer_game_set_sprite_angle"),
    INT_OP("setSpriteScale", "lynxer_game_set_sprite_scale"),
    INT_OP("setSpriteVelocity", "lynxer_game_set_sprite_velocity"),
    INT_OP("setSpriteAngularVelocity", "lynxer_game_set_sprite_angular_velocity"),
    INT_OP("stopSprite", "lynxer_game_stop_sprite"),
    INT_OP("moveSpriteToward", "lynxer_game_move_sprite_toward"),
    INT_OP("faceSpriteTo", "lynxer_game_face_sprite_to"),
    INT_OP("setSpriteAlpha", "lynxer_game_set_sprite_alpha"),
    INT_OP("setSpriteColor", "lynxer_game_set_sprite_color"),
    INT_OP("setSpriteVisible", "lynxer_game_set_sprite_visible"),
    INT_OP("flipSpriteH", "lynxer_game_flip_sprite_h"),
    INT_OP("flipSpriteV", "lynxer_game_flip_sprite_v"),
    INT_OP("destroySprite", "lynxer_game_destroy_sprite"),
    INT_OP("updateSprite", "lynxer_game_update_sprite"),
    INT_OP("drawSprite", "lynxer_game_draw_sprite"),
    INT_OP("drawTexture", "lynxer_game_draw_texture"),
    INT_OP("drawTextureAt", "lynxer_game_draw_texture_at"),
    INT_OP("drawTextureRect", "lynxer_game_draw_texture_rect"),
    INT_OP("spriteCollides", "lynxer_game_sprite_collides"),
    INT_OP("spriteCollidesWithList", "lynxer_game_sprite_collides_with_list"),
    INT_OP("spriteNear", "lynxer_game_sprite_near"),
    FLOAT_OP("getSpriteX", "lynxer_game_get_sprite_x"),
    FLOAT_OP("getSpriteY", "lynxer_game_get_sprite_y"),
    FLOAT_OP("getSpriteAngle", "lynxer_game_get_sprite_angle"),
    FLOAT_OP("getSpriteScale", "lynxer_game_get_sprite_scale"),
    FLOAT_OP("getSpriteWidth", "lynxer_game_get_sprite_width"),
    FLOAT_OP("getSpriteHeight", "lynxer_game_get_sprite_height"),
    FLOAT_OP("getSpriteVX", "lynxer_game_get_sprite_vx"),
    FLOAT_OP("getSpriteVY", "lynxer_game_get_sprite_vy"),
    FLOAT_OP("getSpriteAngularVelocity", "lynxer_game_get_sprite_angular_velocity"),
    FLOAT_OP("spriteDistance", "lynxer_game_sprite_distance"),
    STRING_OP("getSpritePosition", "lynxer_game_get_sprite_position"),

    // Sprite lists.
    INT_OP("makeSpriteList", "lynxer_game_make_sprite_list"),
    INT_OP("addToList", "lynxer_game_add_to_list"),
    INT_OP("removeSpriteFromList", "lynxer_game_remove_sprite_from_list"),
    INT_OP("clearSpriteList", "lynxer_game_clear_sprite_list"),
    INT_OP("getSpriteListCount", "lynxer_game_get_sprite_list_count"),
    INT_OP("drawSpriteList", "lynxer_game_draw_sprite_list"),
    INT_OP("updateSpriteList", "lynxer_game_update_sprite_list"),
    STRING_OP("getCollidingSprites", "lynxer_game_get_colliding_sprites"),

    // Camera.
    INT_OP("makeCamera", "lynxer_game_make_camera"),
    INT_OP("useCamera", "lynxer_game_use_camera"),
    INT_OP("setCameraPos", "lynxer_game_set_camera_pos"),
    INT_OP("zoomCamera", "lynxer_game_zoom_camera"),
    INT_OP("smoothScrollCamera", "lynxer_game_smooth_scroll_camera"),
    INT_OP("resetCamera", "lynxer_game_reset_camera"),
    FLOAT_OP("getCameraX", "lynxer_game_get_camera_x"),
    FLOAT_OP("getCameraY", "lynxer_game_get_camera_y"),
    FLOAT_OP("getCameraZoom", "lynxer_game_get_camera_zoom"),

    // Grid helpers.
    STRING_OP("screenToTile", "lynxer_game_screen_to_tile"),
    STRING_OP("tileToScreen", "lynxer_game_tile_to_screen"),
};

#undef INT_OP
#undef FLOAT_OP
#undef STRING_OP

} // namespace

extern "C" int lynxer_module_attach_v1(const LynxerHostApi* host) {
    if (host == nullptr || host->version != 1 || host->invoke == nullptr) {
        return 1;
    }
    g_host = *host;
    g_attached = true;
    return 0;
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant,
                                     RegisterType) {
    for (const Entry& entry : kEntries) {
        if (function(entry.name, entry.symbol, entry.signature) == 0) {
            return 1;
        }
    }
    return 0;
}
