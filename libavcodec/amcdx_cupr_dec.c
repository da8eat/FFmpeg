#include "libavutil/internal.h"
#include "avcodec.h"

#include "profiles.h"
#include "internal.h"
#include <Windows.h>

typedef void * (* amcdx_pr_decoder_create)();
typedef int (* amcdx_pr_decoder_decode)(void *, void *, int);
typedef unsigned int (* amcdx_pr_get_pitch)(void * );
typedef void (* amcdx_pr_decoder_read)(void * , void ** );
typedef void (* amcdx_pr_decoder_read_pitch)(void *, void **, int *);
typedef void (* amcdx_pr_decoder_destroy)(void * );
typedef unsigned int(* amcdx_pr_get_width)(void * );
typedef unsigned int(* amcdx_pr_get_height)(void *);
typedef int (* amcdx_pr_is_444)(void *);

typedef struct {
    HMODULE m_library;
    amcdx_pr_decoder_create  m_create;
    amcdx_pr_decoder_decode  m_decode;
    amcdx_pr_get_pitch       m_get_pitch;
    amcdx_pr_get_width       m_get_width;
    amcdx_pr_get_height      m_get_height;
    amcdx_pr_decoder_read    m_read;
    amcdx_pr_decoder_read_pitch m_read2d;
    amcdx_pr_decoder_destroy m_destroy;
    amcdx_pr_is_444          m_is_444;

    void * m_decoder;
} AMCDXCUPRContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    AMCDXCUPRContext *ctx = avctx->priv_data;
    avctx->bits_per_raw_sample = 12;

    switch (avctx->codec_tag) {
    case MKTAG('a','p','c','o'):
        avctx->profile = FF_PROFILE_PRORES_PROXY;
        break;
    case MKTAG('a','p','c','s'):
        avctx->profile = FF_PROFILE_PRORES_LT;
        break;
    case MKTAG('a','p','c','n'):
        avctx->profile = FF_PROFILE_PRORES_STANDARD;
        break;
    case MKTAG('a','p','c','h'):
        avctx->profile = FF_PROFILE_PRORES_HQ;
        break;
    case MKTAG('a','p','4','h'):
        avctx->profile = FF_PROFILE_PRORES_4444;
        break;
    case MKTAG('a','p','4','x'):
        avctx->profile = FF_PROFILE_PRORES_XQ;
        break;
    default:
        avctx->profile = FF_PROFILE_UNKNOWN;
        av_log(avctx, AV_LOG_WARNING, "Unknown prores profile %d\n", avctx->codec_tag);
    }

    ctx->m_library = LoadLibraryA("amcdx_cu_prores_decoder.dll");
    if (!ctx->m_library) {
        return -1;
    }

    ctx->m_create     = (amcdx_pr_decoder_create)GetProcAddress(ctx->m_library, "amcdx_cupr_decoder_create");
    ctx->m_decode     = (amcdx_pr_decoder_decode)GetProcAddress(ctx->m_library, "amcdx_cupr_decoder_decode");
    ctx->m_get_pitch  = (amcdx_pr_get_pitch)GetProcAddress(ctx->m_library, "amcdx_cupr_get_pitch");
    ctx->m_get_width  = (amcdx_pr_get_width)GetProcAddress(ctx->m_library, "amcdx_cupr_get_width");
    ctx->m_get_height = (amcdx_pr_get_height)GetProcAddress(ctx->m_library, "amcdx_cupr_get_height");
    ctx->m_read       = (amcdx_pr_decoder_read)GetProcAddress(ctx->m_library, "amcdx_cupr_decoder_read");
    ctx->m_destroy    = (amcdx_pr_decoder_destroy)GetProcAddress(ctx->m_library, "amcdx_cupr_decoder_destroy");
    ctx->m_is_444     = (amcdx_pr_is_444)GetProcAddress(ctx->m_library, "amcdx_cupr_is_444");
    ctx->m_read2d     = (amcdx_pr_decoder_read_pitch)GetProcAddress(ctx->m_library, "amcdx_cupr_decoder_read_pitch");

    ctx->m_decoder = ctx->m_create();

    if (!ctx->m_decoder) {
        return -1;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    AMCDXCUPRContext *ctx = avctx->priv_data;
    AVFrame *frame = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int res = 0;
    unsigned int w, h;

    res = ctx->m_decode(ctx->m_decoder, buf, buf_size);
    if (res) {
        return res;
    }

    w = ctx->m_get_width(ctx->m_decoder);
    h = ctx->m_get_height(ctx->m_decoder);
    //pitch = ctx->m_get_pitch(ctx->m_decoder);

    if (w != avctx->width || h != avctx->height) {
        if ((res = ff_set_dimensions(avctx, w, h)) < 0)
            return res;
    }

    if (ctx->m_is_444(ctx->m_decoder)) {
        avctx->pix_fmt = AV_PIX_FMT_YUV444P12;
    }
    else {
        avctx->pix_fmt = AV_PIX_FMT_YUV422P12;
    }

    if ((res = ff_get_buffer(avctx, frame, 0)) < 0)
        return res;

    ctx->m_read2d(ctx->m_decoder, frame->data, frame->linesize);

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame      = 1;

    return avpkt->size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AMCDXCUPRContext *ctx = avctx->priv_data;

    if (ctx->m_decoder) {
        ctx->m_destroy(ctx->m_decoder);
    }

    if (ctx->m_library) {
        FreeLibrary(ctx->m_library);
    }

    return 0;
}

AVCodec amcdx_cupr_decoder = {
    .name           = "amcdx_cu_prores",
    .long_name      = NULL_IF_CONFIG_SMALL("AMCDX Cuda Prores"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(AMCDXCUPRContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_AUTO_THREADS,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_prores_profiles),
};
