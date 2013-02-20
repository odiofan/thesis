#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/highgui/highgui_c.h>

#include "wht.h"

typedef struct kd_node {
    int *value;
    int nb;
    struct kd_node *left;
    struct kd_node *right;
} kd_node;

typedef struct kd_tree {
    int k;
    kd_node *root;
} kd_tree;

inline static int kdt_compar(const void *a, const void *b, void *opaque)
{
    int off = *(int*)opaque;
    return ((int*)a)[off] - ((int*)b)[off];
}

static kd_node *kdt_new_in(kd_tree *t, int *points, int nb_points,
    int depth, int *order)
{
    if (0 == nb_points) return NULL;
    int axis = order[depth % t->k], median;
    kd_node *node = malloc(sizeof(kd_node));

    if (nb_points <= 8) {
        node->value = points;
        node->left = node->right = NULL;
        node->nb = nb_points;
        return node;
    }
    qsort_r(points, nb_points, t->k*sizeof(int), &kdt_compar, &axis);
    median = nb_points/2;
    node->value = points+median*t->k;
    node->left = kdt_new_in(t, points, median, depth + 1, order);
    node->right = kdt_new_in(t, points+(median+1)*t->k, nb_points - median - 1, depth+1, order);
    node->nb = 1;

    return node;
}

static void kdt_cleanup(kd_node **n)
{
    kd_node *m = *n;
    if (m->left) kdt_cleanup(&m->left);
    if (m->right) kdt_cleanup(&m->right);
    free(m);
    *n = NULL;
}

void kdt_new(kd_tree *t, int *points, int nb_points, int k,
    int *order)
{
    t->k = k; // dimensionality
    t->root = kdt_new_in(t, points, nb_points, k, order);
}

#include <sys/time.h>
static inline double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

void print_kdtree(kd_node *node, int k, int depth, int *order)
{
    int i;
    printf("(%d) ", order[depth%k]);
    for (i = 0; i < depth; i++) {
        printf(" ");
    }
    int *val = node->value;
    for (i = 0; i < k; i++) {
        printf("%d ", val[i]);
    }
    printf("\n");
    if (node->left) print_kdtree(node->left, k, depth+1, order);
    if (node->right) print_kdtree(node->right, k, depth+1, order);
}

typedef struct {
    int min, max, diff, idx;
} dimstats;

static inline int dim_compar(const void *a, const void *b)
{
    return ((dimstats*)a)->diff - ((dimstats*)b)->diff;
}

static int* calc_dimstats(int *points, int nb, int dim)
{
    int i, j, *order = malloc(dim*sizeof(int));
    dimstats *d = malloc(dim*sizeof(dimstats));
    for (j = 0; j < dim; j++) {
        (d+j)->min = INT_MAX;
        (d+j)->max = INT_MIN;
        (d+j)->diff = INT_MAX;
        (d+j)->idx = j;
    }
    for (i = 0; i < nb; i++) {
        for (j = 0; j < dim; j++) {
            int v = *points++;
            dimstats *ds = d+j;
            if (v < ds->min) ds->min = v;
            if (v > ds->max) ds->max = v;
        }
    }

    for (j = 0; j < dim; j++) {
        dimstats *ds = d+j;
        ds->diff = ds->max - ds->min;
    }
    qsort(d, dim, sizeof(dimstats), &dim_compar);
    printf("Ordering: ");
    for (j = 0; j < dim; j++) {
        order[j] = (d+j)->idx;
        printf("%d ", (d+j)->idx);
    }
    printf("\n");
    free(d);
    return order;
}

void quantize(IplImage *img, int n, int *buf, int width)
{
    int i, j, k = n*n;
    int stride = img->widthStep/sizeof(int16_t), *qd = buf;
    int16_t *data = (int16_t*)img->imageData;
    if (!n) memset(data, 0, img->imageSize);
    if (k > width) {
        fprintf(stderr, "quantize: n must be < sqrt(width)\n");
        return;
    }
    int q = 0;
    for (i = 0; i < img->height; i+= 1) {
        if (i%8>=n) continue;
        for (j = 0; j < img->width; j+= 1) {
            if ((i%8 < n) && (j%8 < n)) {
                *qd++ = *(data+i*stride+j);
                k -= 1;
                q+=1;
                if (!k) {
                    k = n*n;
                    buf += width;
                    qd = buf;
                }
            }
        }
    }
}

static IplImage *alignedImage(CvSize dim, int depth, int chan, int align)
{
    int w = dim.width, h = dim.height;
    int dx = align - (w % align), dy =  align - (h % align);
    w += (dx != align) * dx;
    h += (dy != align) * dy; // really unnecessary
    CvSize s = {w, h};
    return cvCreateImage(s, depth, chan);
}


static IplImage *alignedImageFrom(char *file, int align)
{
    IplImage *pre = cvLoadImage(file, CV_LOAD_IMAGE_COLOR);
    IplImage *img = alignedImage(cvGetSize(pre), pre->depth, pre->nChannels, align);
    char *pre_data = pre->imageData;
    char *img_data = img->imageData;
    int i;
    for (i = 0; i < pre->height; i++) {
        memcpy(img_data, pre_data, pre->widthStep);
        img_data += img->widthStep;
        pre_data += pre->widthStep;
    }
    cvReleaseImage(&pre);
    return img;
}


int main()
{
    IplImage *img = alignedImageFrom("eva.jpg", 8);
    CvSize size = cvGetSize(img);
    IplImage *lab = cvCreateImage(size, IPL_DEPTH_8U, 3);
    IplImage *l = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *a = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *b = cvCreateImage(size, IPL_DEPTH_8U, 1);
    IplImage *trans = cvCreateImage(size, IPL_DEPTH_16S, 1);

    int dim = 27, sz = (size.width*size.height/64)*dim;
    int *buf = malloc(sizeof(int)*sz);

    kd_tree kdt;

    cvShowImage("img", img);

    cvCvtColor(img, lab, CV_BGR2Lab);
    cvSplit(lab, l, a, b, NULL);

    wht2d(l, trans);
    quantize(trans, 5, buf, dim);

    wht2d(a, trans);
    quantize(trans, 1, buf+25, dim);

    wht2d(b, trans);
    quantize(trans, 1, buf+26, dim);

    double start = get_time(), end;
    int *order = calc_dimstats(buf, sz/dim, dim);
    kdt_new(&kdt, buf, sz/dim, dim, order);
    end = get_time() - start;

    printf("\nelapsed %f ms\n", end*1000);
    cvWaitKey(0);
    kdt_cleanup(&kdt.root);
    free(buf);
    free(order);
    cvReleaseImage(&img);
    cvReleaseImage(&lab);
    cvReleaseImage(&l);
    cvReleaseImage(&a);
    cvReleaseImage(&b);
    return 0;
}
