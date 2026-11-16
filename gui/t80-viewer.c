// SPDX-License-Identifier: GPL-2.0-only
/*
 * t80-viewer: visualizador do volante Thrustmaster T80 (GTK4 + Cairo).
 *
 * Lê o dispositivo evdev do T80 em modo PS3 (044f:b667) e desenha o volante
 * girando, os pedais e todos os botões em tempo real. Reconecta sozinho
 * quando o volante é desconectado e conectado de novo.
 *
 * Compilar: make        (requer libgtk-4-dev)
 *
 * Uso:
 *   t80-viewer                         abre a janela
 *   t80-viewer --render saida.png      desenha um exemplo numa imagem PNG
 *   t80-viewer --render saida.png --live   usa o estado atual do volante
 */

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <linux/input.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define T80_VID          0x044f
#define T80_PID_PS3      0xb667
#define T80_PID_PS4      0xb668
#define WHEEL_RANGE_DEG  270.0  /* rotação total do T80 */
#define FIXED_RDESC_SIZE 146    /* tamanho do descritor com a correção HID-BPF */

/* Botões na ordem HID (botão 1 = BTN_SOUTH ... botão 13 = BTN_MODE). */
enum {
	B_SQUARE, B_CROSS, B_CIRCLE, B_TRIANGLE,
	B_L1, B_R1, B_L2, B_R2,
	B_SELECT, B_START, B_L3, B_R3, B_PS,
	N_BTN
};

static const char *btn_label[N_BTN] = {
	"", "", "", "", "L1", "R1", "L2", "R2", "SELECT", "START", "L3", "R3", "PS",
};

static const char *btn_code[N_BTN] = {
	"BTN_SOUTH", "BTN_EAST", "BTN_C", "BTN_NORTH", "BTN_WEST", "BTN_Z",
	"BTN_TL", "BTN_TR", "BTN_TL2", "BTN_TR2", "BTN_SELECT", "BTN_START", "BTN_MODE",
};

typedef enum { ST_NONE, ST_PS3, ST_PS4, ST_NOPERM } DevState;

typedef struct {
	GtkWidget *area;
	GtkWidget *subtitle;
	int fd;
	guint watch;
	DevState state;
	gboolean fix_active;
	char evpath[300];
	int x, rx, ry, hatx, haty;
	gboolean btn[N_BTN];
} App;

/* ------------------------------------------------------------------------ */
/* Paleta                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { double r, g, b, a; } RGBA;
#define HEX(h)    ((RGBA){ ((h) >> 16 & 0xff) / 255.0, ((h) >> 8 & 0xff) / 255.0, ((h) & 0xff) / 255.0, 1.0 })
#define HEXA(h, A) ((RGBA){ ((h) >> 16 & 0xff) / 255.0, ((h) >> 8 & 0xff) / 255.0, ((h) & 0xff) / 255.0, (A) })

static const unsigned C_BG     = 0x15171c;
static const unsigned C_PANEL  = 0x1e2229;
static const unsigned C_RAISED = 0x2a2f38;
static const unsigned C_EDGE   = 0x3a404b;
static const unsigned C_TEXT   = 0xe7e9ee;
static const unsigned C_DIM    = 0x8a919d;
static const unsigned C_ACCENT = 0x4cb3ff;
static const unsigned C_BRAKE  = 0xff5f5f;
static const unsigned C_GAS    = 0x3ddc84;
static const unsigned C_WARN   = 0xf5b84a;

static void set_rgba(cairo_t *cr, RGBA c) { cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a); }

/* ------------------------------------------------------------------------ */
/* Primitivas de desenho                                                     */
/* ------------------------------------------------------------------------ */

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	r = fmin(r, fmin(w, h) / 2);
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
	cairo_close_path(cr);
}

/* Texto centrado verticalmente em y; align: 0 = esquerda, 0.5 = centro, 1 = direita. */
static void text(cairo_t *cr, const char *s, double x, double y, double size,
		 PangoWeight weight, double align, RGBA c)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string("Sans");
	int tw, th;

	pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
	pango_font_description_set_weight(fd, weight);
	pango_layout_set_font_description(l, fd);
	pango_layout_set_text(l, s, -1);
	pango_layout_get_pixel_size(l, &tw, &th);
	set_rgba(cr, c);
	cairo_move_to(cr, x - tw * align, y - th / 2.0);
	pango_cairo_show_layout(cr, l);
	pango_font_description_free(fd);
	g_object_unref(l);
}

/* Símbolos dos botões de ação, desenhados com linhas. */
static void draw_symbol(cairo_t *cr, int b, double cx, double cy, double s, RGBA c)
{
	set_rgba(cr, c);
	cairo_set_line_width(cr, fmax(1.2, s * 0.30));
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	switch (b) {
	case B_TRIANGLE:
		cairo_move_to(cr, cx, cy - s * 0.85);
		cairo_line_to(cr, cx + s * 0.9, cy + s * 0.65);
		cairo_line_to(cr, cx - s * 0.9, cy + s * 0.65);
		cairo_close_path(cr);
		break;
	case B_CIRCLE:
		cairo_new_sub_path(cr);
		cairo_arc(cr, cx, cy, s * 0.8, 0, 2 * G_PI);
		break;
	case B_CROSS:
		cairo_move_to(cr, cx - s * 0.7, cy - s * 0.7);
		cairo_line_to(cr, cx + s * 0.7, cy + s * 0.7);
		cairo_move_to(cr, cx + s * 0.7, cy - s * 0.7);
		cairo_line_to(cr, cx - s * 0.7, cy + s * 0.7);
		break;
	case B_SQUARE:
		cairo_rectangle(cr, cx - s * 0.7, cy - s * 0.7, s * 1.4, s * 1.4);
		break;
	}
	cairo_stroke(cr);
}

/* Botão redondo (aceso quando pressionado), com símbolo ou texto. */
static void round_button(cairo_t *cr, double cx, double cy, double r, gboolean on,
			 int sym, const char *label)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
	set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_RAISED));
	cairo_fill_preserve(cr);
	set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_EDGE));
	cairo_set_line_width(cr, fmax(1, r * 0.08));
	cairo_stroke(cr);
	RGBA fg = on ? HEX(C_BG) : HEX(C_TEXT);
	if (sym >= 0)
		draw_symbol(cr, sym, cx, cy, r * 0.42, fg);
	else if (label)
		text(cr, label, cx, cy, r * 0.62, PANGO_WEIGHT_BOLD, 0.5, fg);
}

/* ------------------------------------------------------------------------ */
/* Volante                                                                   */
/* ------------------------------------------------------------------------ */

static void draw_dpad(cairo_t *cr, double cx, double cy, double s, int hx, int hy)
{
	/* braços: cima, direita, baixo, esquerda */
	const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
	const gboolean on[4] = { hy < 0, hx > 0, hy > 0, hx < 0 };
	double w = s * 0.78;

	for (int i = 0; i < 4; i++) {
		double ax = cx + dx[i] * s * 0.95, ay = cy + dy[i] * s * 0.95;
		double bw = dx[i] ? s * 1.1 : w, bh = dy[i] ? s * 1.1 : w;
		rounded_rect(cr, ax - bw / 2, ay - bh / 2, bw, bh, s * 0.18);
		set_rgba(cr, on[i] ? HEX(C_ACCENT) : HEX(C_RAISED));
		cairo_fill_preserve(cr);
		set_rgba(cr, on[i] ? HEX(C_ACCENT) : HEX(C_EDGE));
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, s * 0.22, 0, 2 * G_PI);
	set_rgba(cr, HEX(C_EDGE));
	cairo_fill(cr);
}

static void draw_wheel(cairo_t *cr, App *a, double cx, double cy, double R)
{
	/* 0..255 com centro em 128: lados têm 128 e 127 passos */
	double t = CLAMP(a->x >= 128 ? (a->x - 128) / 127.0 : (a->x - 128) / 128.0, -1.0, 1.0);
	double ang = t * (WHEEL_RANGE_DEG / 2) * G_PI / 180.0;
	cairo_pattern_t *p;

	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, ang);

	/* Borboletas (atrás do aro) */
	for (int side = -1; side <= 1; side += 2) {
		gboolean on = a->btn[side < 0 ? B_L1 : B_R1];
		cairo_save(cr);
		cairo_translate(cr, side * R * 1.08 * 0.866, -R * 1.08 * 0.5);
		cairo_rotate(cr, side * G_PI / 3);
		rounded_rect(cr, -R * 0.085, -R * 0.17, R * 0.17, R * 0.34, R * 0.07);
		set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_RAISED));
		cairo_fill_preserve(cr);
		set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_EDGE));
		cairo_set_line_width(cr, 1.2);
		cairo_stroke(cr);
		cairo_restore(cr);
		text(cr, side < 0 ? "L1" : "R1", side * R * 1.38 * 0.866, -R * 1.38 * 0.5, R * 0.08,
		     PANGO_WEIGHT_BOLD, 0.5, on ? HEX(C_ACCENT) : HEX(C_DIM));
	}

	/* Raios e cubo */
	cairo_new_path(cr);
	rounded_rect(cr, -R * 0.95, -R * 0.15, R * 1.90, R * 0.30, R * 0.12);
	rounded_rect(cr, -R * 0.14, 0, R * 0.28, R * 0.95, R * 0.10);
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0, 0, R * 0.34, 0, 2 * G_PI);
	p = cairo_pattern_create_linear(0, -R * 0.4, 0, R);
	cairo_pattern_add_color_stop_rgb(p, 0, 0x2e / 255.0, 0x33 / 255.0, 0x3d / 255.0);
	cairo_pattern_add_color_stop_rgb(p, 1, 0x1d / 255.0, 0x21 / 255.0, 0x28 / 255.0);
	cairo_set_source(cr, p);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	cairo_fill(cr);
	cairo_pattern_destroy(p);

	/* Aro */
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0, 0, R, 0, 2 * G_PI);
	cairo_set_line_width(cr, R * 0.15);
	p = cairo_pattern_create_linear(0, -R, 0, R);
	cairo_pattern_add_color_stop_rgb(p, 0, 0x3b / 255.0, 0x41 / 255.0, 0x4d / 255.0);
	cairo_pattern_add_color_stop_rgb(p, 1, 0x23 / 255.0, 0x27 / 255.0, 0x2f / 255.0);
	cairo_set_source(cr, p);
	cairo_stroke(cr);
	cairo_pattern_destroy(p);
	/* brilho sutil na parte de cima do aro */
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0, 0, R + R * 0.045, -G_PI * 0.85, -G_PI * 0.15);
	set_rgba(cr, HEXA(0xffffff, 0.06));
	cairo_set_line_width(cr, R * 0.03);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_stroke(cr);

	/* Marcador do centro no topo do aro */
	rounded_rect(cr, -R * 0.035, -R * 1.075, R * 0.07, R * 0.15, R * 0.02);
	set_rgba(cr, HEX(C_ACCENT));
	cairo_fill(cr);

	/* Direcional no raio esquerdo */
	draw_dpad(cr, -R * 0.60, 0, R * 0.085, a->hatx, a->haty);

	/* Botões de ação no raio direito */
	{
		double bx = R * 0.60, d = R * 0.125, r = R * 0.072;
		round_button(cr, bx, -d, r, a->btn[B_TRIANGLE], B_TRIANGLE, NULL);
		round_button(cr, bx + d, 0, r, a->btn[B_CIRCLE], B_CIRCLE, NULL);
		round_button(cr, bx, d, r, a->btn[B_CROSS], B_CROSS, NULL);
		round_button(cr, bx - d, 0, r, a->btn[B_SQUARE], B_SQUARE, NULL);
	}

	/* Cubo: SELECT, START, PS e identificação */
	text(cr, "T80", 0, -R * 0.20, R * 0.08, PANGO_WEIGHT_HEAVY, 0.5, HEX(C_DIM));
	{
		double pw = R * 0.20, ph = R * 0.09;
		const struct { int b; double x; } pills[] = { { B_SELECT, -R * 0.16 }, { B_START, R * 0.16 } };
		for (unsigned i = 0; i < G_N_ELEMENTS(pills); i++) {
			gboolean on = a->btn[pills[i].b];
			rounded_rect(cr, pills[i].x - pw / 2, -R * 0.02 - ph / 2, pw, ph, ph / 2);
			set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_RAISED));
			cairo_fill_preserve(cr);
			set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_EDGE));
			cairo_set_line_width(cr, 1);
			cairo_stroke(cr);
			text(cr, pills[i].b == B_SELECT ? "SEL" : "START", pills[i].x, -R * 0.02,
			     R * 0.05, PANGO_WEIGHT_BOLD, 0.5, on ? HEX(C_BG) : HEX(C_DIM));
		}
	}
	round_button(cr, 0, R * 0.16, R * 0.085, a->btn[B_PS], -1, "PS");

	/* L3 / R3 no raio de baixo */
	round_button(cr, 0, R * 0.52, R * 0.07, a->btn[B_L3], -1, "L3");
	round_button(cr, 0, R * 0.72, R * 0.07, a->btn[B_R3], -1, "R3");

	cairo_restore(cr);

	/* Leitura do ângulo e barra do eixo X */
	{
		char buf[64];
		double by = cy + R * 1.22, bw = R * 1.5, bh = 6;

		double deg = t * WHEEL_RANGE_DEG / 2;
		snprintf(buf, sizeof buf, fabs(deg) < 0.5 ? "0°" : "%+.0f°", deg);
		text(cr, buf, cx, by, R * 0.13, PANGO_WEIGHT_BOLD, 0.5, HEX(C_TEXT));

		by += R * 0.14;
		rounded_rect(cr, cx - bw / 2, by - bh / 2, bw, bh, bh / 2);
		set_rgba(cr, HEX(C_RAISED));
		cairo_fill(cr);
		double fx = cx + t * bw / 2;
		rounded_rect(cr, fmin(cx, fx), by - bh / 2, fabs(fx - cx), bh, bh / 2);
		set_rgba(cr, HEX(C_ACCENT));
		cairo_fill(cr);
		cairo_rectangle(cr, cx - 0.75, by - bh, 1.5, bh * 2);
		set_rgba(cr, HEX(C_DIM));
		cairo_fill(cr);

		snprintf(buf, sizeof buf, "ABS_X  %d", a->x);
		text(cr, buf, cx, by + 16, 11, PANGO_WEIGHT_NORMAL, 0.5, HEX(C_DIM));
	}
}

/* ------------------------------------------------------------------------ */
/* Pedais                                                                    */
/* ------------------------------------------------------------------------ */

static void draw_pedal(cairo_t *cr, double x, double y, double w, double h,
		       const char *name, const char *axis, int value, gboolean digital,
		       const char *dlabel, RGBA color)
{
	char buf[32];
	double frac = CLAMP(value / 255.0, 0, 1);
	double tw = fmin(w * 0.55, 64), tx = x + (w - tw) / 2;
	double ty = y + 64, th = h - 64 - 58;

	text(cr, name, x + w / 2, y + 10, 12, PANGO_WEIGHT_BOLD, 0.5, HEX(C_DIM));
	snprintf(buf, sizeof buf, "%.0f%%", frac * 100);
	text(cr, buf, x + w / 2, y + 38, 26, PANGO_WEIGHT_BOLD, 0.5,
	     value > 0 ? color : HEX(C_TEXT));

	/* trilho */
	rounded_rect(cr, tx, ty, tw, th, 14);
	set_rgba(cr, HEX(C_PANEL));
	cairo_fill_preserve(cr);
	set_rgba(cr, HEX(C_EDGE));
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	/* marcas de 25% */
	for (int i = 1; i < 4; i++) {
		double my = ty + th * i / 4.0;
		cairo_rectangle(cr, tx + 6, my - 0.5, tw - 12, 1);
		set_rgba(cr, HEXA(0xffffff, 0.05));
		cairo_fill(cr);
	}

	/* preenchimento */
	if (frac > 0) {
		double fh = fmax(th * frac, 4);
		cairo_pattern_t *p = cairo_pattern_create_linear(0, ty + th - fh, 0, ty + th);
		cairo_pattern_add_color_stop_rgba(p, 0, color.r, color.g, color.b, 1.0);
		cairo_pattern_add_color_stop_rgba(p, 1, color.r, color.g, color.b, 0.55);
		cairo_save(cr);
		rounded_rect(cr, tx, ty, tw, th, 14);
		cairo_clip(cr);
		cairo_rectangle(cr, tx, ty + th - fh, tw, fh);
		cairo_set_source(cr, p);
		cairo_fill(cr);
		cairo_restore(cr);
		cairo_pattern_destroy(p);
	}

	/* valor bruto e indicador digital */
	snprintf(buf, sizeof buf, "%s  %d", axis, value);
	text(cr, buf, x + w / 2, ty + th + 18, 11, PANGO_WEIGHT_NORMAL, 0.5, HEX(C_DIM));

	double dy = ty + th + 40;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w / 2 - 14, dy, 4.5, 0, 2 * G_PI);
	set_rgba(cr, digital ? color : HEX(C_RAISED));
	cairo_fill(cr);
	text(cr, dlabel, x + w / 2 - 4, dy, 11, PANGO_WEIGHT_BOLD, 0,
	     digital ? color : HEX(C_DIM));
}

/* ------------------------------------------------------------------------ */
/* Lista de botões                                                           */
/* ------------------------------------------------------------------------ */

static void draw_chips(cairo_t *cr, App *a, double x, double y, double w, int cols,
		       double ch, double gap)
{
	double cw = (w - (cols - 1) * gap) / cols;

	for (int i = 0; i < N_BTN; i++) {
		double cx = x + (i % cols) * (cw + gap), cy = y + (i / cols) * (ch + gap);
		gboolean on = a->btn[i];
		RGBA fg = on ? HEX(C_BG) : HEX(C_TEXT);

		rounded_rect(cr, cx, cy, cw, ch, 10);
		set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_PANEL));
		cairo_fill_preserve(cr);
		set_rgba(cr, on ? HEX(C_ACCENT) : HEX(C_EDGE));
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);

		if (i <= B_TRIANGLE)
			draw_symbol(cr, i, cx + cw / 2, cy + ch * 0.38, ch * 0.13, fg);
		else
			text(cr, btn_label[i], cx + cw / 2, cy + ch * 0.38,
			     cw < 64 ? 10 : 12, PANGO_WEIGHT_BOLD, 0.5, fg);
		text(cr, btn_code[i], cx + cw / 2, cy + ch * 0.76, cw < 64 ? 7.5 : 8.5,
		     PANGO_WEIGHT_NORMAL, 0.5, on ? HEXA(C_BG, 0.8) : HEX(C_DIM));
	}
}

/* ------------------------------------------------------------------------ */
/* Desenho principal                                                         */
/* ------------------------------------------------------------------------ */

static void draw_scene(cairo_t *cr, App *a, int w, int h)
{
	const double m = 24, gap = 8, ch = 48;
	int cols = (w - 2 * m) / N_BTN >= 62 ? N_BTN : 7;
	int rows = (N_BTN + cols - 1) / cols;
	double chips_h = rows * ch + (rows - 1) * gap;
	double top = m, bottom = h - m - chips_h - 20;
	double pedal_w = fmin(280, w * 0.30);
	double wheel_w = w - 2 * m - pedal_w;

	/* volante (deixa espaço para a leitura de ângulo embaixo) */
	double avail_h = bottom - top;
	double R = fmin(wheel_w / 2.9, (avail_h - 34) / 2.95);
	draw_wheel(cr, a, m + wheel_w / 2, top + R * 1.45, R);

	/* pedais */
	double px = w - m - pedal_w;
	rounded_rect(cr, px, top, pedal_w, avail_h, 18);
	set_rgba(cr, HEXA(0xffffff, 0.02));
	cairo_fill(cr);

	int brake = a->rx, gas = a->ry;
	double ptop = top + 8;
	if (!a->fix_active && a->state == ST_PS3) {
		/* sem a correção, só existe o sinal liga/desliga dos pedais */
		brake = a->btn[B_L2] ? 255 : 0;
		gas = a->btn[B_R2] ? 255 : 0;
		text(cr, "correção inativa: pedais só liga/desliga", px + pedal_w / 2, top + 14,
		     10.5, PANGO_WEIGHT_BOLD, 0.5, HEX(C_WARN));
		ptop += 18;
	}
	draw_pedal(cr, px, ptop, pedal_w / 2, avail_h - (ptop - top), "FREIO", "ABS_RX",
		   brake, a->btn[B_L2], "L2", HEX(C_BRAKE));
	draw_pedal(cr, px + pedal_w / 2, ptop, pedal_w / 2, avail_h - (ptop - top),
		   "ACELERADOR", "ABS_RY", gas, a->btn[B_R2], "R2", HEX(C_GAS));

	/* botões */
	draw_chips(cr, a, m, h - m - chips_h, w - 2 * m, cols, ch, gap);
}

static void draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
	App *a = data;
	(void)area;

	set_rgba(cr, HEX(C_BG));
	cairo_paint(cr);

	if (a->state == ST_PS3) {
		draw_scene(cr, a, w, h);
		return;
	}

	/* Desconectado: cena esmaecida com valores neutros + mensagem */
	App idle = *a;
	idle.x = 128;
	idle.rx = idle.ry = idle.hatx = idle.haty = 0;
	memset(idle.btn, 0, sizeof idle.btn);
	idle.fix_active = TRUE;
	cairo_push_group(cr);
	draw_scene(cr, &idle, w, h);
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, 0.18);

	const char *title, *msg;
	switch (a->state) {
	case ST_PS4:
		title = "T80 em modo PS4";
		msg = "Nesse modo volante e pedais não funcionam.\nMude a chave lateral para PS3 e reconecte o USB.";
		break;
	case ST_NOPERM:
		title = "Sem permissão";
		msg = "Não foi possível abrir o dispositivo de entrada do T80.\nVerifique as permissões de /dev/input/event*.";
		break;
	default:
		title = "Aguardando o T80";
		msg = "Conecte o volante pelo USB com a chave em PS3.";
		break;
	}
	double bw = fmin(460, w - 48), bh = 118, bx = (w - bw) / 2, by = (h - bh) / 2;
	rounded_rect(cr, bx, by, bw, bh, 18);
	set_rgba(cr, HEX(C_PANEL));
	cairo_fill_preserve(cr);
	set_rgba(cr, a->state == ST_NONE ? HEX(C_EDGE) : HEX(C_WARN));
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);
	text(cr, title, w / 2.0, by + 34, 18, PANGO_WEIGHT_BOLD, 0.5,
	     a->state == ST_NONE ? HEX(C_TEXT) : HEX(C_WARN));

	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *fd = pango_font_description_from_string("Sans 10.5");
	int tw, th;
	pango_layout_set_font_description(l, fd);
	pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
	pango_layout_set_text(l, msg, -1);
	pango_layout_get_pixel_size(l, &tw, &th);
	set_rgba(cr, HEX(C_DIM));
	cairo_move_to(cr, (w - tw) / 2.0, by + 74 - th / 2.0);
	pango_cairo_show_layout(cr, l);
	pango_font_description_free(fd);
	g_object_unref(l);
}

/* ------------------------------------------------------------------------ */
/* Dispositivo                                                               */
/* ------------------------------------------------------------------------ */

static gboolean read_hex(const char *path, unsigned *out)
{
	FILE *f = fopen(path, "r");
	gboolean ok;

	if (!f)
		return FALSE;
	ok = fscanf(f, "%x", out) == 1;
	fclose(f);
	return ok;
}

static long file_size_by_reading(const char *path)
{
	char buf[1024];
	long total = 0;
	size_t n;
	FILE *f = fopen(path, "rb");

	if (!f)
		return -1;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		total += n;
	fclose(f);
	return total;
}

/* Procura o T80 em /sys/class/input. Preenche o evdev e o caminho do descritor. */
static DevState find_t80(char *evpath, size_t evlen, char *rdesc, size_t rdlen)
{
	DIR *d = opendir("/sys/class/input");
	struct dirent *e;
	DevState st = ST_NONE;

	if (!d)
		return ST_NONE;
	while ((e = readdir(d))) {
		char p[512];
		unsigned vid, pid;

		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		snprintf(p, sizeof p, "/sys/class/input/%s/device/id/vendor", e->d_name);
		if (!read_hex(p, &vid) || vid != T80_VID)
			continue;
		snprintf(p, sizeof p, "/sys/class/input/%s/device/id/product", e->d_name);
		if (!read_hex(p, &pid))
			continue;
		if (pid == T80_PID_PS3) {
			snprintf(evpath, evlen, "/dev/input/%s", e->d_name);
			snprintf(rdesc, rdlen, "/sys/class/input/%s/device/device/report_descriptor",
				 e->d_name);
			st = ST_PS3;
			break;
		}
		if (pid == T80_PID_PS4)
			st = ST_PS4;
	}
	closedir(d);
	return st;
}

static void update_status(App *a)
{
	char buf[400];

	switch (a->state) {
	case ST_PS3:
		snprintf(buf, sizeof buf, "Conectado · modo PS3 · correção %s · %s",
			 a->fix_active ? "ativa" : "INATIVA", a->evpath);
		break;
	case ST_PS4:
		snprintf(buf, sizeof buf, "Modo PS4 detectado · mude a chave para PS3");
		break;
	case ST_NOPERM:
		snprintf(buf, sizeof buf, "Sem permissão para ler %s", a->evpath);
		break;
	default:
		snprintf(buf, sizeof buf, "Desconectado");
		break;
	}
	gtk_label_set_text(GTK_LABEL(a->subtitle), buf);
	gtk_widget_queue_draw(a->area);
}

static void app_disconnect(App *a, gboolean from_watch)
{
	if (a->watch && !from_watch)
		g_source_remove(a->watch);
	a->watch = 0;
	if (a->fd >= 0)
		close(a->fd);
	a->fd = -1;
	a->state = ST_NONE;
	update_status(a);
}

static gboolean on_input(gint fd, GIOCondition cond, gpointer data)
{
	App *a = data;
	struct input_event ev[64];
	gboolean dirty = FALSE;

	for (;;) {
		ssize_t n = read(fd, ev, sizeof ev);

		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			break;
		if (n <= 0) {
			app_disconnect(a, TRUE);
			return G_SOURCE_REMOVE;
		}
		for (size_t i = 0; i < (size_t)n / sizeof *ev; i++) {
			struct input_event *e = &ev[i];

			if (e->type == EV_ABS) {
				switch (e->code) {
				case ABS_X:     a->x = e->value; break;
				case ABS_RX:    a->rx = e->value; break;
				case ABS_RY:    a->ry = e->value; break;
				case ABS_HAT0X: a->hatx = e->value; break;
				case ABS_HAT0Y: a->haty = e->value; break;
				}
			} else if (e->type == EV_KEY && e->code >= BTN_SOUTH &&
				   e->code < BTN_SOUTH + N_BTN) {
				a->btn[e->code - BTN_SOUTH] = e->value != 0;
			} else if (e->type == EV_SYN) {
				dirty = TRUE;
			}
		}
	}
	if (cond & (G_IO_HUP | G_IO_ERR)) {
		app_disconnect(a, TRUE);
		return G_SOURCE_REMOVE;
	}
	if (dirty)
		gtk_widget_queue_draw(a->area);
	return G_SOURCE_CONTINUE;
}

static int abs_value(int fd, int code, int fallback)
{
	struct input_absinfo ai;

	return ioctl(fd, EVIOCGABS(code), &ai) == 0 ? ai.value : fallback;
}

static void app_connect(App *a)
{
	char rdesc[512];
	unsigned char keys[(KEY_MAX + 7) / 8];
	DevState st = find_t80(a->evpath, sizeof a->evpath, rdesc, sizeof rdesc);

	if (st != ST_PS3) {
		if (st != a->state) {
			a->state = st;
			update_status(a);
		}
		return;
	}

	a->fd = open(a->evpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (a->fd < 0) {
		a->state = ST_NOPERM;
		update_status(a);
		return;
	}
	a->state = ST_PS3;
	a->fix_active = file_size_by_reading(rdesc) == FIXED_RDESC_SIZE;

	/* Estado inicial. Logo após o kernel reler o descritor, ABS_X pode valer 0
	 * até o primeiro relatório chegar; nesse caso assume o centro. */
	a->x = abs_value(a->fd, ABS_X, 128);
	if (a->x == 0)
		a->x = 128;
	a->rx = abs_value(a->fd, ABS_RX, 0);
	a->ry = abs_value(a->fd, ABS_RY, 0);
	a->hatx = abs_value(a->fd, ABS_HAT0X, 0);
	a->haty = abs_value(a->fd, ABS_HAT0Y, 0);
	memset(keys, 0, sizeof keys);
	ioctl(a->fd, EVIOCGKEY(sizeof keys), keys);
	for (int i = 0; i < N_BTN; i++) {
		int c = BTN_SOUTH + i;
		a->btn[i] = (keys[c / 8] >> (c % 8)) & 1;
	}

	a->watch = g_unix_fd_add(a->fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_input, a);
	update_status(a);
}

static gboolean on_tick(gpointer data)
{
	App *a = data;

	if (a->fd < 0)
		app_connect(a);
	return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------------ */
/* Janela                                                                    */
/* ------------------------------------------------------------------------ */

static void on_activate(GtkApplication *gapp, gpointer data)
{
	App *a = data;
	GtkWidget *win, *header, *titles, *title;
	GtkCssProvider *css;

	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
	css = gtk_css_provider_new();
	gtk_css_provider_load_from_string(css,
		"window.t80 { background: #15171c; }"
		".t80-sub { font-size: 0.85em; opacity: 0.65; }");
	gtk_style_context_add_provider_for_display(gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	win = gtk_application_window_new(gapp);
	gtk_widget_add_css_class(win, "t80");
	gtk_window_set_title(GTK_WINDOW(win), "Visualizador T80");
	gtk_window_set_default_size(GTK_WINDOW(win), 980, 660);

	header = gtk_header_bar_new();
	titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(titles, GTK_ALIGN_CENTER);
	title = gtk_label_new("Thrustmaster T80");
	gtk_widget_add_css_class(title, "title");
	a->subtitle = gtk_label_new("");
	gtk_widget_add_css_class(a->subtitle, "t80-sub");
	gtk_box_append(GTK_BOX(titles), title);
	gtk_box_append(GTK_BOX(titles), a->subtitle);
	gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), titles);
	gtk_window_set_titlebar(GTK_WINDOW(win), header);

	a->area = gtk_drawing_area_new();
	gtk_widget_set_size_request(a->area, 640, 480);
	gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->area), draw, a, NULL);
	gtk_window_set_child(GTK_WINDOW(win), a->area);

	app_connect(a);
	update_status(a);
	g_timeout_add_seconds(1, on_tick, a);

	gtk_window_present(GTK_WINDOW(win));
}

/* Desenha a interface num PNG (sem abrir janela). */
static int render_png(const char *path, gboolean live, int w, int h)
{
	App a = { .fd = -1, .x = 128, .state = ST_NONE };
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(s);
	cairo_status_t st;

	if (live) {
		char rdesc[512];
		a.state = find_t80(a.evpath, sizeof a.evpath, rdesc, sizeof rdesc);
		if (a.state == ST_PS3) {
			int fd = open(a.evpath, O_RDONLY | O_CLOEXEC);
			if (fd >= 0) {
				unsigned char keys[(KEY_MAX + 7) / 8] = { 0 };
				a.fix_active = file_size_by_reading(rdesc) == FIXED_RDESC_SIZE;
				a.x = abs_value(fd, ABS_X, 128);
				a.rx = abs_value(fd, ABS_RX, 0);
				a.ry = abs_value(fd, ABS_RY, 0);
				a.hatx = abs_value(fd, ABS_HAT0X, 0);
				a.haty = abs_value(fd, ABS_HAT0Y, 0);
				ioctl(fd, EVIOCGKEY(sizeof keys), keys);
				for (int i = 0; i < N_BTN; i++)
					a.btn[i] = (keys[(BTN_SOUTH + i) / 8] >> ((BTN_SOUTH + i) % 8)) & 1;
				close(fd);
			} else {
				a.state = ST_NOPERM;
			}
		}
	} else {
		/* exemplo: virando à direita, acelerando, X e R1 pressionados */
		a.state = ST_PS3;
		a.fix_active = TRUE;
		a.x = 178;
		a.rx = 0;
		a.ry = 196;
		a.haty = -1;
		a.btn[B_CROSS] = a.btn[B_R1] = a.btn[B_R2] = TRUE;
	}

	draw(NULL, cr, w, h, &a);
	st = cairo_surface_write_to_png(s, path);
	cairo_destroy(cr);
	cairo_surface_destroy(s);
	if (st != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "erro ao gravar %s: %s\n", path, cairo_status_to_string(st));
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	App a = { .fd = -1, .x = 128, .state = ST_NONE };

	if (argc >= 3 && strcmp(argv[1], "--render") == 0)
		return render_png(argv[2], argc >= 4 && strcmp(argv[3], "--live") == 0, 980, 620);

	GtkApplication *app = gtk_application_new("io.github.saulogp.T80Viewer",
						  G_APPLICATION_DEFAULT_FLAGS);
	int status;

	g_signal_connect(app, "activate", G_CALLBACK(on_activate), &a);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	if (a.fd >= 0)
		close(a.fd);
	g_object_unref(app);
	return status;
}
