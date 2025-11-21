#include "ai_draw.h"
#include "dev_manager.h"
#include "aicam_error.h"
#include "mem.h"
#include "debug.h"
#include "common_utils.h"

#define NUMBER_COLORS 9
#define COLOR_HEAD COLOR_GREEN
#define COLOR_ARMS COLOR_BLUE
#define COLOR_TRUNK COLOR_MAGENTA
#define COLOR_LEGS COLOR_YELLOW
#define MPE_YOLOV8_PP_CONF_THRESHOLD (0.6000000000f)

__attribute__((unused)) static const int bindings[][3] = {
  {15, 13, COLOR_LEGS},
  {13, 11, COLOR_LEGS},
  {16, 14, COLOR_LEGS},
  {14, 12, COLOR_LEGS},
  {11, 12, COLOR_TRUNK},
  { 5, 11, COLOR_TRUNK},
  { 6, 12, COLOR_TRUNK},
  { 5,  6, COLOR_ARMS},
  { 5,  7, COLOR_ARMS},
  { 6,  8, COLOR_ARMS},
  { 7,  9, COLOR_ARMS},
  { 8, 10, COLOR_ARMS},
  { 1,  2, COLOR_HEAD},
  { 0,  1, COLOR_HEAD},
  { 0,  2, COLOR_HEAD},
  { 1,  3, COLOR_HEAD},
  { 2,  4, COLOR_HEAD},
  { 3,  5, COLOR_HEAD},
  { 4,  6, COLOR_HEAD},
};

static void convert_value(uint32_t width, uint32_t height, float_t xi, float_t yi, int *xo, int *yo)
{
  *xo = (int) (width * xi);
  *yo = (int) (height * yi);
}


static int clamp_point(uint32_t width, uint32_t height, int *x, int *y)
{
  int xi = *x;
  int yi = *y;

  if (*x < 0)
    *x = 0;
  if (*y < 0)
    *y = 0;
  if (*x >= width)
    *x = width - 1;
  if (*y >= height)
    *y = height - 1;

  return (xi != *x) || (yi != *y);
}
/**
 * @brief Initializes the drawing configuration.
 *
 * @param draw_conf The configuration to use. If this is NULL, the function
 *                  returns -1.
 *
 * @return 0 on success, -1 on failure.
 */
int mpe_draw_init(mpe_draw_conf_t *mpe_conf)
{
    int ret;
    if(mpe_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_init invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }
    mpe_conf->color = COLOR_YELLOW;
    mpe_conf->line_width = 2;
    mpe_conf->box_line_width = 2;
    mpe_conf->dot_width = 10;

    draw_fontsetup_param_t font_param = {0};
    /* set 12 font */
    if(mpe_conf->font.data){
        hal_mem_free(mpe_conf->font.data);
        mpe_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &mpe_conf->font;
    ret = device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    if(ret < 0){
        LOG_DRV_ERROR("mpe_draw_init failed\r\n");
        if(mpe_conf->binds){
            hal_mem_free(mpe_conf->binds);
            mpe_conf->binds = NULL;
        }
        return -1;
    }
    return 0;
}

int mpe_draw_deinit(mpe_draw_conf_t *mpe_conf)
{
    if(mpe_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_deinit invalid param\r\n");
        return -1;
    }
    if(mpe_conf->binds){
        hal_mem_free(mpe_conf->binds);
        mpe_conf->binds = NULL;
    }

    if(mpe_conf->font.data){
        hal_mem_free(mpe_conf->font.data);
        mpe_conf->font.data = NULL;
    }
    return 0;
}

int mpe_draw_result(mpe_draw_conf_t *mpe_conf, mpe_detect_t *result)
{
    if(mpe_conf == NULL || result == NULL){
        LOG_DRV_ERROR("mpe_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    if(result->num_connections > 0){
        if(mpe_conf->binds != NULL){
            hal_mem_free(mpe_conf->binds);
            mpe_conf->binds = NULL;
        }
        mpe_conf->num_binds = result->num_connections;
        mpe_conf->binds = hal_mem_alloc_fast(sizeof(mpe_draw_bind_t) * mpe_conf->num_binds);
        if(mpe_conf->binds == NULL){
            LOG_DRV_ERROR("mpe_draw_result failed\r\n");
            return -1;
        }
        for(int i = 0; i < mpe_conf->num_binds; i++){
            mpe_conf->binds[i].keypoint1 = result->keypoint_connections[i*2 + 0];
            mpe_conf->binds[i].keypoint2 = result->keypoint_connections[i*2 + 1];
            mpe_conf->binds[i].color = COLOR_GREEN;
        }
    }
    int x0, y0, w, h;
    convert_value(mpe_conf->image_width, mpe_conf->image_height, result->x, result->y, &x0, &y0);
    convert_value(mpe_conf->image_width, mpe_conf->image_height, result->width, result->height, &w, &h);
    clamp_point(mpe_conf->image_width, mpe_conf->image_height, &x0, &y0);

    draw_rect_param_t rect_param = {0};
    rect_param.p_dst = mpe_conf->p_dst;
    rect_param.dst_width = mpe_conf->image_width;
    rect_param.dst_height = mpe_conf->image_height;
    rect_param.x_pos = x0;
    rect_param.y_pos = y0;
    rect_param.width = w;
    rect_param.height = h;
    rect_param.line_width = mpe_conf->box_line_width;
    rect_param.color = mpe_conf->color;
    if(rect_param.x_pos + rect_param.width > rect_param.dst_width){
        rect_param.width = rect_param.dst_width - rect_param.x_pos - mpe_conf->line_width;
    }
    if(rect_param.y_pos + rect_param.height > rect_param.dst_height){
        rect_param.height = rect_param.dst_height - rect_param.y_pos - mpe_conf->line_width;
    }
    device_ioctl(draw, DRAW_CMD_RECT, (uint8_t *)&rect_param, sizeof(draw_rect_param_t));

    draw_printf_param_t print_param = {0};
    snprintf(print_param.str, sizeof(print_param.str), "%s %5.2f", result->class_name, result->conf);
    print_param.p_font = &mpe_conf->font;
    print_param.p_dst = mpe_conf->p_dst;
    print_param.dst_width = mpe_conf->image_width;
    print_param.dst_height = mpe_conf->image_height;
    print_param.x_pos = x0 + mpe_conf->line_width;
    print_param.y_pos = y0 + mpe_conf->line_width;
    device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

    int nb_keypoints = result->nb_keypoints;
    int keypoint_x[nb_keypoints], keypoint_y[nb_keypoints];
    bool keypoint_valid[nb_keypoints];
    for (int i = 0; i < nb_keypoints; i++) {
        keypoint_valid[i] = false;
        float x = result->keypoints[i].x;
        float y = result->keypoints[i].y;
        if (result->keypoints[i].conf >= MPE_YOLOV8_PP_CONF_THRESHOLD &&
            x >= 0 && y >= 0 && x <= 1 && y <= 1) {
            convert_value(mpe_conf->image_width, mpe_conf->image_height, x, y, &keypoint_x[i], &keypoint_y[i]);
            keypoint_valid[i] = true;
        }
    }

    draw_line_param_t line_param = {0};
    for (int i = 0; i < mpe_conf->num_binds; i++) {
        int k1 = mpe_conf->binds[i].keypoint1;
        int k2 = mpe_conf->binds[i].keypoint2;
        if (k1 < nb_keypoints && k2 < nb_keypoints && keypoint_valid[k1] && keypoint_valid[k2]) {
            line_param.p_dst = mpe_conf->p_dst;
            line_param.dst_width = mpe_conf->image_width;
            line_param.dst_height = mpe_conf->image_height;
            line_param.x1 = keypoint_x[k1];
            line_param.y1 = keypoint_y[k1];
            line_param.x2 = keypoint_x[k2];
            line_param.y2 = keypoint_y[k2];
            line_param.line_width = mpe_conf->line_width;
            line_param.color = mpe_conf->binds[i].color;
            device_ioctl(draw, DRAW_CMD_LINE, (uint8_t *)&line_param, sizeof(draw_line_param_t));
        }
    }

    draw_dot_param_t dot_param = {0};
    for (int i = 0; i < nb_keypoints; i++) {
        if (keypoint_valid[i]) {

            snprintf(print_param.str, sizeof(print_param.str), "%d", i);
            print_param.p_font = &mpe_conf->font;
            print_param.p_dst = mpe_conf->p_dst;
            print_param.dst_width = mpe_conf->image_width;
            print_param.dst_height = mpe_conf->image_height;
            print_param.x_pos = keypoint_x[i] + mpe_conf->dot_width;
            print_param.y_pos = keypoint_y[i];
            device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

            dot_param.p_dst = mpe_conf->p_dst;
            dot_param.dst_width = mpe_conf->image_width;
            dot_param.dst_height = mpe_conf->image_height;
            dot_param.x_pos = keypoint_x[i];
            dot_param.y_pos = keypoint_y[i];
            dot_param.dot_width = mpe_conf->dot_width;
            dot_param.color = mpe_conf->color;
            device_ioctl(draw, DRAW_CMD_DOT, (uint8_t *)&dot_param, sizeof(draw_dot_param_t));
        }
    }

    return 0;
}



int od_draw_init(od_draw_conf_t *od_conf)
{
    if(od_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_init invalid param\r\n");
        return -1;
    }
    od_conf->color = COLOR_GREEN;
    od_conf->line_width = 4;
    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    draw_fontsetup_param_t font_param = {0};
    /* set 12 font */
    if(od_conf->font.data){
        hal_mem_free(od_conf->font.data);
        od_conf->font.data = NULL;
    }
    font_param.p_font_in = &Font16;
    font_param.p_font = &od_conf->font;
    device_ioctl(draw, DRAW_CMD_FONT_SETUP, (uint8_t *)&font_param, sizeof(draw_fontsetup_param_t));
    return 0;
}


int od_draw_deinit(od_draw_conf_t *od_conf)
{
    if(od_conf == NULL){
        LOG_DRV_ERROR("mpe_draw_deinit invalid param\r\n");
        return -1;
    }

    if(od_conf->font.data){
        hal_mem_free(od_conf->font.data);
        od_conf->font.data = NULL;
    }
    
    return 0;
}
int od_draw_result(od_draw_conf_t *od_conf, od_detect_t *result)
{

    int x0, y0;
    // int x1, y1;
    int w, h;

    draw_printf_param_t print_param = {0};
    draw_rect_param_t rect_param = {0};

    if(od_conf == NULL || result == NULL){
        LOG_DRV_ERROR("mpe_draw_result invalid param\r\n");
        return -1;
    }

    device_t *draw = device_find_pattern(DRAW_DEVICE_NAME, DEV_TYPE_VIDEO);
    if(draw == NULL){
        return -1;
    }

    convert_value(od_conf->image_width, od_conf->image_height, result->x, result->y, &x0, &y0);
    convert_value(od_conf->image_width, od_conf->image_height, result->width, result->height, &w, &h);

    clamp_point(od_conf->image_width, od_conf->image_height, &x0, &y0);
    // clamp_point(od_conf->image_width, od_conf->image_height, &w, &y1);
    //draw box
    rect_param.p_dst = od_conf->p_dst;
    rect_param.dst_width = od_conf->image_width;
    rect_param.dst_height = od_conf->image_height;
    rect_param.x_pos = x0;
    rect_param.y_pos = y0;
    rect_param.width = w;
    rect_param.height = h;
    rect_param.line_width = od_conf->line_width;
    rect_param.color = od_conf->color;
    if(rect_param.x_pos + rect_param.width > od_conf->image_width){
        rect_param.width = od_conf->image_width - rect_param.x_pos - od_conf->line_width;
    }
    if(rect_param.y_pos + rect_param.height > od_conf->image_height){
        rect_param.height = od_conf->image_height - rect_param.y_pos - od_conf->line_width;
    }
    device_ioctl(draw, DRAW_CMD_RECT, (uint8_t *)&rect_param, sizeof(draw_rect_param_t));

    // draw_colorrect_param_t colorrect_param = {0};
    // colorrect_param.p_dst = od_conf->p_dst;
    // colorrect_param.dst_width = od_conf->image_width;
    // colorrect_param.dst_height = od_conf->image_height;
    // colorrect_param.x_pos = x0;
    // colorrect_param.y_pos = y0;
    // colorrect_param.width = w;
    // colorrect_param.height = h;
    // colorrect_param.color = od_conf->color;
    // colorrect_param.alpha = 128;
    // if(colorrect_param.x_pos + colorrect_param.width > od_conf->image_width){
    //     colorrect_param.width = od_conf->image_width - colorrect_param.x_pos - od_conf->line_width;
    // }
    // if(colorrect_param.y_pos + colorrect_param.height > od_conf->image_height){
    //     colorrect_param.height = od_conf->image_height - colorrect_param.y_pos - od_conf->line_width;
    // }
    // device_ioctl(draw, DRAW_CMD_BLEND_COLOR_RECT, (uint8_t *)&colorrect_param, sizeof(draw_colorrect_param_t));

    snprintf(print_param.str, sizeof(print_param.str), "%s", result->class_name);

    // int text_len = strlen(print_param.str);
    // int text_width = text_len * od_conf->font.width;
    // int font_height = od_conf->font.height;

    int text_x = x0 + od_conf->line_width;
    int text_y = y0 + od_conf->line_width;

    print_param.p_font = &od_conf->font;
    print_param.p_dst = od_conf->p_dst;
    print_param.dst_width = od_conf->image_width;
    print_param.dst_height = od_conf->image_height;
    print_param.x_pos = text_x;
    print_param.y_pos = text_y;
    device_ioctl(draw, DRAW_CMD_PRINTF, (uint8_t *)&print_param, sizeof(draw_printf_param_t));

    return 0;

}