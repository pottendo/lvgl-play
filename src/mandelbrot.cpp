#include <log.h>
#include <lvgl.h>
#include <stdlib.h>
#include <complex>
#include <SDL2/SDL.h>
#include <unistd.h>
#include <iostream>

const int no_threads = 9;
const int pal_size = 1024;
lv_color_t col_pal[pal_size];
static lv_obj_t *canvas;
//static lv_draw_rect_dsc_t rect_dsc;
static lv_coord_t mark_x1, mark_y1, mark_x2, mark_y2;
static lv_obj_t *rect = nullptr;
static lv_point_t pts[5];
static lv_style_t style_line;
static double last_xr, last_yr, ssw, ssh, transx, transy;

#define IMG_W 702
#define IMG_H 702

const int max_iter = pal_size;
//double SCALEX = 0.75, SCALEY = 0.25, MOVEX = 0.1, MOVEY = 0.6;
double SCALEX = 0.5, SCALEY = 0.5, MOVEX = 0.0, MOVEY = 0.5;

struct tparam_t
{
    int tno;
    double xl, xh, yl, yh, incx, incy;
    int xoffset, yoffset;
    char s[256];
    SDL_semaphore *go;
    SDL_semaphore *sem;

    tparam_t(int t, double x1, double x2, double y1, double y2, double ix, double iy, int xo, int yo, SDL_semaphore *se)
    {
        tno = t;
        xl = x1;
        xh = x2;
        yl = y1;
        yh = y2;
        incx = ix;
        incy = iy;
        xoffset = xo;
        yoffset = yo;
        sem = se;
        sprintf(s, "t=%d, xl=%f,xh=%f,yl=%f,yh=%f,incx=%f,incy=%f\n", tno, xl, xh, yl, yh, incx, incy);
        go = SDL_CreateSemaphore(1);
        if (!go)
        {
            log_msg("Create semaphore failed...\n");
            throw("sem failed");
        }
        log_msg(this->toString());
    }
    char *toString() { return s; }
};

static tparam_t *tp[no_threads];

static lv_obj_t *create_canvas(int w = LV_HOR_RES_MAX, int h = LV_VER_RES_MAX)
{
    lv_obj_t *c = lv_canvas_create(lv_scr_act(), nullptr);
    lv_obj_align(c, lv_scr_act(), LV_ALIGN_CENTER, -w / 2, -h / 2);
    lv_color_t *cbuf = (lv_color_t *)malloc(sizeof(lv_color_t) * LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h));
    lv_canvas_set_buffer(c, cbuf, w, h, LV_IMG_CF_TRUE_COLOR);

    return c;
}

// abs() would calc sqr() as well, we don't need that for this fractal
inline double abs2(std::complex<double> f)
{
    double r = f.real(), i = f.imag();
    return r * r + i * i;
}

static int mandel_calc_point(double x, double y)
{
    const std::complex<double> point{x, y};
    // we divide by the image dimensions to get values smaller than 1
    // then apply a translation
    // std::cout << "calc: " << point << '\n';
    std::complex<double> z = point;
    unsigned int nb_iter = 1;
    while (abs2(z) < 4 && nb_iter <= max_iter)
    {
        z = z * z + point;
        nb_iter++;
    }
    if (nb_iter < max_iter)
        return (nb_iter);
    else
        return 0;
}

void mandel_helper(double xl, double xh, double yl, double yh, double incx, double incy, int xo, int yo)
{
    double x, y;
    int xk = xo;
    int yk = yo;
    for (x = xl; x < xh; x += incx)
    {
        for (y = yl; y < yh; y += incy)
        {
            int d = mandel_calc_point(x, y);
            lv_canvas_set_px(canvas, xk, yk, col_pal[d]);
            //mandel_buffer[x][y] = mandel_calc_point(x, y, TFT_WIDTH, TFT_HEIGHT);
            yk++;
        }
        yk = yo;
        xk++;
    }
}

int mandel_wrapper(void *param)
{
    tparam_t *p = (tparam_t *)param;
    //Serial.print(pcTaskGetTaskName(NULL)); Serial.print(p->tno); Serial.println(" started.");
    //Serial.println(p->toString());
    //log_msg(p->toString());
    // Wait to be kicked off by mainthread
    //log_msg("thread %d waiting for kickoff\n", p->tno);
    SDL_SemWait(p->go);
    //log_msg("starting thread %d\n", p->tno);
    mandel_helper(p->xl, p->xh, p->yl, p->yh, p->incx, p->incy, p->xoffset, p->yoffset);
    SDL_SemPost(p->sem); // report we've done our job

    return 0;
}

void mandel_setup(const int thread_no, double sx, double sy, double tx, double ty)
{
    int t = 0;
    last_xr = (tx - sx);
    last_yr = (ty - sy);
    ssw = last_xr / IMG_W;
    ssh = last_yr / IMG_H;
    transx = sx;
    transy = sy;
    double stepx = (IMG_W / thread_no) * ssw;
    double stepy = (IMG_H / thread_no) * ssh;
    SDL_Thread *th;
    if (thread_no > 64)
    {
        log_msg("too many threads... giving up.");
        return;
    }
    SDL_semaphore *master_sem = SDL_CreateSemaphore(thread_no);

    for (int tx = 0; tx < thread_no; tx++)
    {
        int xoffset = (IMG_H / thread_no) * tx;
        for (int ty = 0; ty < thread_no; ty++)
        {
            int yoffset = (IMG_H / thread_no) * ty;
            tp[t] = new tparam_t(t,
                                 tx * stepx + transx, tx * stepx + stepx + transx,
                                 ty * stepy + transy, ty * stepy + stepy + transy,
                                 ssw, ssh, xoffset, yoffset,
                                 master_sem);
            th = SDL_CreateThread(mandel_wrapper, "T", tp[t]);
            t++;
        }
    }
}

void mandelbrot_set(void)
{
    log_msg("mandelbrot set...\n");
    canvas = create_canvas(IMG_W, IMG_H);

    for (int i = 0; i < pal_size; i++)
    {
        uint8_t g = i % 256;
        uint8_t r = (256 - i) % 256;
        uint8_t b = (r + g) % 256;
        col_pal[i] = LV_COLOR_MAKE(r, g, b);
    }
#if 0
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_opa = LV_OPA_50;
    rect_dsc.bg_color = LV_COLOR_GRAY;
    rect_dsc.border_opa = LV_OPA_COVER;
    rect_dsc.border_color = LV_COLOR_WHITE;
#endif
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, LV_STATE_DEFAULT, 2);
    lv_style_set_line_color(&style_line, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_style_set_line_opa(&style_line, LV_STATE_DEFAULT, LV_OPA_COVER);

    mandel_setup(sqrt(no_threads), -1.5, -1.0, 0.5, 1.0);
    for (int i = 0; i < no_threads; i++)
    {
        SDL_SemPost(tp[i]->go);
        //usleep(250 * 1000);
    }
}

void select_start(lv_point_t &p)
{
    mark_x1 = p.x - ((LV_HOR_RES_MAX - IMG_W) / 2);
    mark_y1 = p.y - ((LV_VER_RES_MAX - IMG_H) / 2);
    if (mark_x1 < 0)
        mark_x1 = 0;
    if (mark_y1 < 0)
        mark_y1 = 0;
    log_msg("rect start: %dx%d\n", mark_x1, mark_y1);
}

void select_end(lv_point_t &p)
{
    mark_x2 = p.x - ((LV_HOR_RES_MAX - IMG_W) / 2);
    mark_y2 = p.y - ((LV_VER_RES_MAX - IMG_H) / 2);
    if (mark_x2 < 0)
        mark_x2 = 0;
    if (mark_y2 < 0)
        mark_y2 = 0;
    log_msg("rect coord: %dx%d - %dx%d\n", mark_x1, mark_y1, mark_x2, mark_y2);

    mandel_setup(sqrt(no_threads),
                 mark_x1 * ssw + transx,
                 mark_y1 * ssh + transy,
                 mark_x2 * ssw + transx,
                 mark_y2 * ssh + transy);
    if (rect)
        lv_obj_del(rect);
    rect = nullptr;
    mark_x1 = -1;
    mark_x2 = mark_x1;
    mark_y2 = mark_y1;
}

void select_update(lv_point_t &p)
{
    if (mark_x1 < 0)
    {
        if (rect)
            lv_obj_del(rect);
        rect = nullptr;
        return;
    }
    lv_coord_t tx = mark_x2;
    lv_coord_t ty = mark_y2;
    mark_x2 = p.x - ((LV_HOR_RES_MAX - IMG_W) / 2);
    mark_y2 = p.y - ((LV_VER_RES_MAX - IMG_H) / 2);
    if (mark_x2 < 0)
        mark_x2 = 0;
    if (mark_y2 < 0)
        mark_y2 = 0;
    if ((tx == mark_x2) && (ty == mark_y2))
        return;
    if (rect)
    {
        lv_obj_del(rect);
    }
    rect = lv_line_create(lv_scr_act(), nullptr);
    pts[0] = {mark_x1, mark_y1};
    pts[1] = {mark_x2, mark_y1};
    pts[2] = {mark_x2, mark_y2};
    pts[3] = {mark_x1, mark_y2};
    pts[4] = {mark_x1, mark_y1};
    lv_line_set_points(rect, pts, 5);
    lv_obj_add_style(rect, LV_LINE_PART_MAIN, &style_line);
    lv_obj_set_top(rect, true);
    lv_obj_align(rect, canvas, LV_ALIGN_IN_TOP_LEFT, 0, 0);

    //log_msg("rect coord: %dx%d - %dx%d\n", mark_x1, mark_y1, mark_x2, mark_y2);
}
