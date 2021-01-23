#include <log.h>
#include <lvgl.h>
#include <stdlib.h>
#include <complex>
#include <SDL2/SDL.h>
#include <unistd.h>

const int no_threads = 64;
const int pal_size = 1024;
lv_color_t col_pal[pal_size];
static lv_obj_t *canvas;
#define IMG_W 400
#define IMG_H 400

const int max_iter = pal_size;
//float SCALEX = 0.75, SCALEY = 0.25, MOVEX = 0.1, MOVEY = 0.6;
float SCALEX = 0.5, SCALEY = 0.5, MOVEX = 0.0, MOVEY = 0.5;

struct tparam_t
{
    int tno;
    int xl, xh, yl, yh;
    char s[256];
    SDL_semaphore *go;
    SDL_semaphore *sem;

    tparam_t(int t, int x1, int x2, int y1, int y2, SDL_semaphore *se)
    {
        tno = t;
        xl = x1;
        xh = x2;
        yl = y1;
        yh = y2;
        sem = se;
        sprintf(s, "t=%d, xl=%d,xh=%d,yl=%d,yh=%d\n", tno, xl, xh, yl, yh);
        go = SDL_CreateSemaphore(0);
        if (!go) {
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
inline float abs2(std::complex<float> f)
{
    float r = f.real(), i = f.imag();
    return r * r + i * i;
}

static int mandel_calc_point(int x, int y, int width, int height)
{
    const std::complex<float> point((float)x / (width * SCALEX) - (1.5 + MOVEX), (float)y / (height * SCALEY) - (0.5 + MOVEY));
    // we divide by the image dimensions to get values smaller than 1
    // then apply a translation
    std::complex<float> z = point;
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

void mandel_helper(int xl, int xh, int yl, int yh)
{
    int x, y;

    for (x = xl; x <= xh; x++)
    {
        for (y = yl; y <= yh; y++)
        {
            int d = mandel_calc_point(x, y, IMG_W, IMG_H);
            lv_canvas_set_px(canvas, x, y, col_pal[d]);
            //mandel_buffer[x][y] = mandel_calc_point(x, y, TFT_WIDTH, TFT_HEIGHT);
        }
    }
}

int mandel_wrapper(void *param)
{
    tparam_t *p = (tparam_t *)param;
    //Serial.print(pcTaskGetTaskName(NULL)); Serial.print(p->tno); Serial.println(" started.");
    //Serial.println(p->toString());
    //log_msg(p->toString());
    for (;;)
    {
        usleep(5000);
        // Wait to be kicked off by mainthread
        //log_msg("thread %d waiting for kickoff\n", p->tno);
        SDL_SemWait(p->go);
        log_msg("starting thread %d\n", p->tno);
        mandel_helper(p->xl, p->xh, p->yl, p->yh);
        SDL_SemPost(p->sem); // report we've done our job
    }
}

void mandel_setup(const int thread_no)
{
    int t = 0;
    int msc = IMG_W / thread_no;
    SDL_Thread *th;
    if (thread_no > 64)
    {
        log_msg("too many threads... giving up.");
        return;
    }
    SDL_semaphore *master_sem = SDL_CreateSemaphore(thread_no);

    for (int tx = 0; tx < thread_no; tx++)
    {
        for (int ty = 0; ty < thread_no; ty++)
        {
            tp[t] = new tparam_t(t, tx * msc, tx * msc + msc - 1,
                                 ty * msc, ty * msc + msc - 1,
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
        uint8_t b = (r * g) % 256;
        col_pal[i] = LV_COLOR_MAKE(r, g, b);
    }
    mandel_setup(sqrt(no_threads));
    for (int i = 0; i < no_threads; i++)
    {
        SDL_SemPost(tp[i]->go);
        //usleep(250 * 1000);
    }
}
