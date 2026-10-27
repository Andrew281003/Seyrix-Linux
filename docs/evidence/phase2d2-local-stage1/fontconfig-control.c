#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <string.h>

int main(int argc, char **argv) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 240, 80);
    cairo_t *cr = cairo_create(surface);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Sans 12");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, "Fontconfig cache diagnostic", -1);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    pango_font_description_free(font);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    if (argc > 1 && strcmp(argv[1], "fc-fini") == 0) {
        FcFini();
    }
    return 0;
}
