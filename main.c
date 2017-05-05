#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_connection.h>
#include <interface/mmal/util/mmal_component_wrapper.h>
#include <stdio.h>


/* Setting 1 to this aligns-up the width on save_image() func. */
#define ISP_IGNORES_CROPPING 0
#define ISP_IN_ENCODING  MMAL_ENCODING_BAYER_SBGGR10P
#define ISP_OUT_ENCODING MMAL_ENCODING_RGB24

#define ISP_IN_WIDTH     (512 + 64 + 16)
#define ISP_OUT_WIDTH    (ISP_IN_WIDTH)
#define ISP_IN_HEIGHT    (512)
#define ISP_OUT_HEIGHT   (ISP_IN_HEIGHT)
#define OUTPUT_FILE "out.ppm"


#define _check(x) \
    do { \
        int ret = (x); \
        if (ret != MMAL_SUCCESS) { \
            fprintf(stderr, "%s:%d:%s: %s: 0x%08x\n", __FILE__, __LINE__, __func__, #x, ret); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void save_image(uint8_t *image, int width, const int height, FILE *fp)
{
    int i, j;

#if ISP_IGNORES_CROPPING
    width = VCOS_ALIGN_UP(width, 32);
    printf("%s: ISP ignores cropping setting; aligning width to %d\n",
           __func__, width);
#endif /* ISP_IGNORES_CROPPING */
    fprintf(fp, "P3\n%d %d\n255\n", width, height);
    for (i = 0; i < height; i ++) {
        for (j = 0; j < width; j ++) {
            fprintf(fp, "%u %u %u\n", image[0], image[1], image[2]);
            image += 3;
        }
    }
}

static MMAL_STATUS_T config_port(MMAL_PORT_T *port, const MMAL_FOURCC_T encoding, const int width, const int height)
{
    port->format->encoding = encoding;
    port->format->es->video.width  = VCOS_ALIGN_UP(width,  32);
    port->format->es->video.height = VCOS_ALIGN_UP(height, 16);
    port->format->es->video.crop.x = 0;
    port->format->es->video.crop.y = 0;
    port->format->es->video.crop.width  = width;
    port->format->es->video.crop.height = height;
    printf("%s: %s: video:%dx%d video.crop:%dx%d\n", __func__,
           port->name,
           port->format->es->video.width,
           port->format->es->video.height,
           port->format->es->video.crop.width,
           port->format->es->video.crop.height);
    return mmal_port_format_commit(port);
}

int main()
{
    MMAL_WRAPPER_T *cpw_isp = NULL;
    MMAL_PORT_T *input = NULL, *output = NULL;
    MMAL_BUFFER_HEADER_T *header = NULL;
    FILE *fp_out = NULL;

    const int raw_width  = ALIGN_UP((ISP_IN_WIDTH * 5 + 3) >> 2, 32);
    const int raw_height = ISP_IN_HEIGHT;

    fp_out = fopen(OUTPUT_FILE, "w");
    if (fp_out == NULL) {
        fprintf(stderr, "error: fopen: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    _check(mmal_wrapper_create(&cpw_isp, "vc.ril.isp"));
    input  = cpw_isp->input[0];
    output = cpw_isp->output[0];
    _check(config_port(input,  ISP_IN_ENCODING,  ISP_IN_WIDTH,  ISP_IN_HEIGHT));
    _check(config_port(output, ISP_OUT_ENCODING, ISP_OUT_WIDTH, ISP_OUT_HEIGHT));
    _check(mmal_wrapper_port_enable(input,  MMAL_WRAPPER_FLAG_PAYLOAD_ALLOCATE));
    _check(mmal_wrapper_port_enable(output, MMAL_WRAPPER_FLAG_PAYLOAD_ALLOCATE));

    while (mmal_wrapper_buffer_get_empty(output, &header, 0) == MMAL_SUCCESS)
        _check(mmal_port_send_buffer(output, header));

    _check(mmal_wrapper_buffer_get_empty(input, &header, 0));
    header->length = raw_height * raw_width;
    /* Fill the bayer buffer all to ones. */
    memset(header->data, 0xff, header->length);
    header->flags = MMAL_BUFFER_HEADER_FLAG_EOS;
    _check(mmal_port_send_buffer(input, header));

    _check(mmal_wrapper_buffer_get_full(output, &header, MMAL_WRAPPER_FLAG_WAIT));
    printf("Output header length is %d\n", header->length);
    printf("    If ISP correctly does cropping, this should be %d\n",
           ISP_OUT_WIDTH * ISP_OUT_HEIGHT * 3);
    printf("    Otherwise, this should be %d\n",
           VCOS_ALIGN_UP(ISP_OUT_WIDTH, 32) * ISP_OUT_HEIGHT * 3);
    save_image(header->data, ISP_OUT_WIDTH, ISP_OUT_HEIGHT, fp_out);
    if (fclose(fp_out) != 0) {
        fprintf(stderr, "error: fclose: %s (%d)\n", strerror(errno), errno);
        exit(EXIT_FAILURE);
    }
    printf("Saved image to %s\n", OUTPUT_FILE);

    return 0;
}
