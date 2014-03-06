#include <arrayfire.h>
#include <af/utils.h>
#include <stdio.h>
#include <vector>
#include <algorithm>

#define OCL_NOT_SUP(fn) do {                            \
    char errstr[1024];                                  \
    snprintf(errstr, sizeof(errstr),                    \
             "%s not supported for OpenCL\n", fn);      \
    throw af::exception(errstr);                        \
  } while(0)

using namespace af;
using namespace std;

typedef struct node {
    void *curr;
    struct node *left;
    struct node *right;
    bool cleanup;
} Node;

vector<Node> vec;

void destroy(void *ptr)
{
    for (int i = 0; i < vec.size(); i++){

        if (vec[i].curr == ptr) {
            if (vec[i].left ) destroy(vec[i].left->curr );
            if (vec[i].right) destroy(vec[i].right->curr);
            delete (array *)vec[i].curr;
            vec[i].cleanup = true;
            return;
        }

    }
}

int getnode(void *ptr)
{
    for (int i = 0; i < vec.size(); i++) {
        if (vec[i].curr == ptr) return i;
    }
    return -1;
}

bool iscleanup(Node n) { return n.cleanup; }

void cleanup(void *ptr)
{
    int l = getnode(ptr);
    if (l < 0) return;

    if (vec[l].left ) destroy(vec[l].left->curr );
    if (vec[l].right) destroy(vec[l].right->curr);
    vec[l].cleanup = true;

    vec.erase(std::remove_if(vec.begin(), vec.end(), iscleanup), vec.end());
}

void vec_add(void *dst, void *in1=NULL, void *in2=NULL)
{
    int l = getnode(in1);
    int r = getnode(in2);
    Node *left  = (l < 0) ? NULL : &vec[l];
    Node *right = (r < 0) ? NULL : &vec[r];
    Node n = {dst, left, right, false};
    vec.push_back(n);
}

extern "C" {

    void af_device_info_() { af::info(); return; }
    void af_device_get_(int *n) { *n = deviceget(); return; }
    void af_device_set_(int *n) { deviceset(*n); return; }
    void af_device_count_(int *n) {*n = devicecount(); return; }

    void af_device_eval_(void **arr) { af::eval(*(array *)arr); return; }
    void af_device_sync_() { af::sync(); return; }

    void af_timer_start_() { timer::start(); return; }
    void af_timer_stop_(double *elapsed) { *elapsed = timer::stop(); return; }

#define DEVICE(X, ty)                               \
    void af_arr_device_##X##_(void **ptr, ty *a,    \
                              int *shape, int *err) \
    {                                               \
        try {                                       \
            *ptr = (void *)new array();             \
            array *tmp = (array *)*ptr;             \
            *tmp = array(shape[0], shape[1],        \
                         shape[2], shape[3], a);    \
            vec_add(*ptr);                          \
        } catch (af::exception& ex) {               \
            *err = 1;                               \
            printf("%s\n", ex.what());              \
            exit(-1);                               \
        }                                           \
    }                                               \

    DEVICE(s, float);
    DEVICE(d, double);
    DEVICE(c, cfloat);
    DEVICE(z, cdouble);

    void af_arr_copy_(void **dst, void **src, int *err)
    {
        try {
            if (*dst) delete (array *)*dst;
            *dst = *src;
            cleanup(*dst);
        } catch (af::exception& ex) {
            *err = 2;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

#define GEN(fn)                                     \
    void af_arr_##fn##_(void **ptr, int *x,         \
                        int *fty, int *err)         \
    {                                               \
        try {                                       \
            dtype ty = (dtype)(*fty - 1);           \
            *ptr = (void *)new array();             \
            array *tmp = (array *)*ptr;             \
            *tmp = fn(x[0], x[1], x[2], x[3], ty);  \
            vec_add(*ptr);                          \
        } catch (af::exception& ex) {               \
            *err = 3;                               \
            printf("%s\n", ex.what());              \
            exit(-1);                               \
        }                                           \
    }                                               \

    GEN(randu);
    GEN(randn);
    GEN(identity);
#undef GEN

void af_arr_constant_(void **ptr, int *val, int *x, int *fty, int *err)
{
    try {
        dtype ty = (dtype)(*fty - 1);
        *ptr = (void *)new array();
        array *tmp = (array *)*ptr;
        *tmp = constant(*val, x[0], x[1], x[2], x[3], ty);
        vec_add(*ptr);
    } catch (af::exception& ex) {
        *err = 3;
        printf("%sn", ex.what());
        exit(-1);
    }
}

#define HOST(X, ty)                                                     \
  void af_arr_host_##X##_(ty *a, void **ptr,                            \
                          int *dim, int *err)                           \
  {   try {                                                             \
      array tmp = *(array *)*ptr;                                       \
      dim4 d = tmp.dims();                                              \
      int bytes = tmp.elements() * sizeof(ty);                          \
      tmp.host((void *)a);                                              \
    } catch (af::exception& ex) {                                       \
      *err = 5;                                                         \
      printf("%s\n", ex.what());                                        \
      exit(-1);                                                         \
    }                                                                   \
  }                                                                     \

    HOST(s, float);
    HOST(d, double);
    HOST(c, cfloat);
    HOST(z, cdouble);

#define SCOP(fn, op)                                \
    void af_arr_sc##fn##_(void **dst, void **src,   \
                          double *a, int *err)      \
    {                                               \
        try {                                       \
            *dst = (void *)new array();             \
            array *in = (array *)*src;              \
            array *out  = (array *)*dst;            \
            *out = *in op *a;                       \
            vec_add(*dst, *src);                    \
        } catch (af::exception& ex) {               \
            *err = 6;                               \
            printf("%s\n", ex.what());              \
            exit(-1);                               \
        }                                           \
    }                                               \

    SCOP(plus  , +)
    SCOP(minus , -)
    SCOP(times , *)
    SCOP(div   , /)
    SCOP(le    , <=)
    SCOP(lt    , < )
    SCOP(ge    , >=)
    SCOP(gt    , > )
    SCOP(eq    , ==)
    SCOP(ne    , !=)

#define ELOP(fn, op)                                \
    void af_arr_el##fn##_(void **dst, void **src,   \
                          void **tsd, int *err)     \
    {                                               \
        try {                                       \
            *dst = (void *)new array();             \
            array *left = (array *)*src;            \
            array *right = (array *)*tsd;           \
            array *out  = (array *)*dst;            \
            *out = *left op *right;                 \
            vec_add(*dst, *src, *tsd);              \
        } catch (af::exception& ex) {               \
            *err = 7;                               \
            printf("%s\n", ex.what());              \
            exit(-1);                               \
        }                                           \
    }                                               \

    ELOP(plus  , +)
    ELOP(minus , -)
    ELOP(times , *)
    ELOP(div   , /)
    ELOP(le    , <=)
    ELOP(lt    , < )
    ELOP(ge    , >=)
    ELOP(gt    , > )
    ELOP(eq    , ==)
    ELOP(ne    , !=)
    ELOP(and   , &&)
    ELOP(or    , ||)

    void af_arr_negate_(void **dst, void **src, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = -(*in);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 8;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_not_(void **dst, void **src, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = !(*in);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 8;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_scpow_(void **dst, void **src, double *a, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = pow(*in , *a);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 8;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_elpow_(void **dst, void **src, void **tsd, int *err)
    {
        try {
            *dst = (void *)new array();
            array *left = (array *)*src;
            array *right = (array *)*tsd;
            array *out  = (array *)*dst;
            *out = pow(*left , *right);
            vec_add(*dst, *src, *tsd);
        } catch (af::exception& ex) {
            *err = 9;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

#define OP(fn)                                  \
    void af_arr_##fn##_(void **dst, void **src, \
                        int *err)               \
    {                                           \
        try {                                   \
            *dst = (void *)new array();         \
            array *in = (array *)*src;          \
            array *out  = (array *)*dst;        \
            *out = fn(*in);                     \
            vec_add(*dst, *src);                \
        } catch (af::exception& ex) {           \
            *err = 10;                          \
            printf("%s\n", ex.what());          \
            exit(-1);                           \
        }                                       \
    }                                           \

    OP(sin);
    OP(cos);
    OP(tan);
    OP(log);
    OP(abs);
    OP(exp);
    OP(sort);
    OP(upper);
    OP(lower);
    OP(diag);
    OP(real);
    OP(imag);
    OP(complex);
    OP(conjg);
    OP(mean);
    OP(var);
    OP(stdev);

#undef OP

#define OP(fn)                                  \
    void af_arr_##fn##_(void **dst, void **src, \
                        int *dim, int *err)     \
    {                                           \
        try {                                   \
            *dst = (void *)new array();         \
            array *in = (array *)*src;          \
            array *out  = (array *)*dst;        \
            *out = fn(*in, (*dim - 1));         \
            vec_add(*dst, *src);                \
        } catch (af::exception& ex) {           \
            *err = 10;                          \
            printf("%s\n", ex.what());          \
            exit(-1);                           \
        }                                       \
    }                                           \

    OP(sum);
    OP(mul);
    OP(min);
    OP(max);
    OP(anytrue);
    OP(alltrue);


    void af_arr_moddims_(void **dst, void **src, int *x, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = moddims(*in, x[0], x[1], x[2], x[3]);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%sn", ex.what());
            exit(-1);
        }
    }

    void af_arr_tile_(void **dst, void **src, int *x, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            dim4 dims(x[0], x[1], x[2], x[3]);
            *out = tile(*in, dims);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%sn", ex.what());
            exit(-1);
        }
    }

    void af_arr_t_(void **dst, void **src, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = (*in).T();
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_h_(void **dst, void **src, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = (*in).H();
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_reorder_(void **dst, void **src, int *shape, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = reorder(*in, shape[0]-1, shape[1]-1, shape[2]-1, shape[3]-1);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_complex2_(void **dst, void **re, void **im, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in1 = (array *)*re;
            array *in2 = (array *)*im;
            array *out  = (array *)*dst;
            *out = complex(*in1, *in2);
            vec_add(*dst, *re, *im);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_norm_(double *dst, void **src, int *err)
    {
        try {
            array *in = (array *)*src;
            if ((*in).type() == f32)
                *dst = (double)norm<float>(*in);
            else
                *dst = (double)norm<double>(*in);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_pnorm_(double *dst, void **src, float *p, int *err)
    {
        try {
            array *in = (array *)*src;
            if ((*in).type() == f32)
                *dst = (double)norm<float>(*in, *p);
            else
                *dst = (double)norm<double>(*in, *p);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_matmul_(void **dst, void **src, void **tsd, int *err)
    {
        try {

            *dst = (void *)new array();
            array *left = (array *)*src;
            array *right = (array *)*tsd;
            array *out  = (array *)*dst;
            *out = matmul(*left, *right);
            vec_add(*dst, *src, *tsd);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_matpow_(void **dst, void **src, double *a, int *err)
    {
        try {
            *dst = (void *)new array();
            array *in = (array *)*src;
            array *out  = (array *)*dst;
            *out = matpow(*in , *a);
            vec_add(*dst, *src);
        } catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_lu_(void **l, void **u, void **p, void **in, int *err)
    {
        try {
            *l = (void *)new array();
            *u = (void *)new array();
            *p = (void *)new array();

            array *L = (array *)*l, *U = (array *)*u, *P = (array *)*p, *A = (array *)*in;
            lu(*L, *U, *P, *A);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }


    void af_arr_lu_inplace_(void **in, int *err)
    {
        try {
        #ifdef AFCL
            OCL_NOT_SUP("lu_inplace");
        #else
            array *A = (array *)*in;
            int m = A->dims(0), n = A->dims(1);
            int *d_piv = array::alloc<int>(m);
            switch(A->type()) {
            case f32:
                *err = (int) af_lu_S(d_piv, NULL, A->device<float>(), m, n, 1); break;
            case f64:
                *err = (int) af_lu_D(d_piv, NULL, A->device<double>(), m, n, 1); break;
            case c32:
                *err = (int) af_lu_C(d_piv, NULL, A->device<cfloat>(), m, n, 1); break;
            case c64:
                *err = (int) af_lu_Z(d_piv, NULL, A->device<cdouble>(), m, n, 1); break;
            default:
                *err = 11;
                printf("Unsupported type\n");
                return;
            }
            array::free(d_piv);
       #endif
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }


    void af_arr_qr_(void **q, void **r, void **in, int *err)
    {
        try {
            *q = (void *)new array();
            *r = (void *)new array();

            array *Q = (array *)*q, *R = (array *)*r, *A = (array *)*in;
            qr(*Q, *R, *A);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }


    void af_arr_cholesky_(void **r, void **in, int *err)
    {
        try {
            *r = (void *)new array();
            unsigned info;
            array *R = (array *)*r, *A = (array *)*in;
            *R = cholesky(info, *A, false);
            *err = info;
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }


    void af_arr_cholesky_inplace_(void **r, int *err)
    {
        try {
        #ifdef AFCL
            OCL_NOT_SUP("cholesky_inplace");
        #else
            unsigned info;
            array *R = (array *)*r;
            unsigned n = R->dims(0);
            switch(R->type()) {
            case f32:
                *err = (int)af_cholesky_S(NULL, &info, n, R->device<float>(), true, 1);
                break;
            case f64:
                *err = (int)af_cholesky_D(NULL, &info, n, R->device<double>(), true, 1);
                break;
            case c32:
                *err = (int)af_cholesky_C(NULL, &info, n, R->device<cfloat>(), true, 1);
                break;
            case c64:
                *err = (int)af_cholesky_Z(NULL, &info, n, R->device<cdouble>(), true, 1);
                break;
            }
            *err = info;
        #endif
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_hessenberg_(void **r, void **in, int *err)
    {
        try {
            *r = (void *)new array();
            array *R = (array *)*r, *A = (array *)*in;
            *R = hessenberg(*A);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_eigen_(void **val, void **vec, void **in, int *err)
    {
        try {
            *val = (void *)new array();

            array *Val = (array *)*val;
            array *Vec = (array *)*vec;
            array *A = (array *)*in;
            eigen(*Val, *Vec, *A);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_singular_(void **s, void **u, void **v, void **in, int *err)
    {
        try {
            *s = (void *)new array();
            *u = (void *)new array();
            *v = (void *)new array();

            array *S = (array *)*s, *U = (array *)*u, *V = (array *)*v, *A = (array *)*in;
            svd(*S, *U, *V, *A);
        }
        catch (af::exception& ex) {
            printf("%s\n", ex.what());
            exit(1);
        }
        *err = 0;
    }

    void af_arr_inverse_(void **r, void **in, int *err)
    {
        try {
            *r = (void *)new array();
            array *R = (array *)*r, *A = (array *)*in;
            *R = inverse(*A);
            vec_add((void *)r, *in);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_solve_(void **x, void **a, void **b, int *err)
    {
        try {
            *x = (void *)new array();

            array *A = (array *)*a, *B = (array *)*b, *X = (array *)*x;
            *X = solve(*A, *B);
            vec_add(*x, *a, *b);
        }
        catch (af::exception& ex) {
            *err = 11;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }
    void af_arr_get_(void **out, void **in, void **d0, void **d1, int *d2, int *d3, int *err)
    {
        try {
            *out = (void *)new array();
            array *R = (array *)*out;
            array A = *(array *)*in;
            array idx0 = (*(array *)*d0) - 1;
            array idx1 = seq(A.dims(1));
            if (d1 != NULL && *d1 != NULL) idx1 = (*(array *)*d1) - 1;
            int idx2 = (*d2 - 1) + (*d3 - 1) * (A.dims(0) * A.dims(1) * A.dims(2));
            *R = A(idx0, idx1, idx2);
            vec_add(*out, *in);
        } catch (af::exception& ex) {
            *err = 12;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_set_(void **out, void **in, void **d0, void **d1, int *d2, int *d3, int *err)
    {
        try {
            array *R = (array *)*out;
            array A = *(array *)*in;
            array idx0 = (*(array *)*d0) - 1;
            array idx1 = seq(R->dims(1));
            if (d1 != NULL && *d1 != NULL) idx1 = (*(array *)*d1) - 1;
            int idx2 = (*d2 - 1) + (*d3 - 1) * (R->dims(0) * R->dims(1) * R->dims(2));
            (*R)(idx0, idx1, idx2) = A;
        } catch (af::exception& ex) {
            *err = 12;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_idx_seq_(void **out, int *first, int *last, int *step, int *err)
    {
        try {
            *out = (void *)new array();
            array *R = (array *)*out;
            *R = array(seq(*first, *step, *last));
            vec_add((void *)out);
        } catch (af::exception& ex) {
            *err = 12;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_idx_vec_(void **out, int* indices, int *numel, int *err)
    {
        try {
            *out = (void *)new array();
            array *R = (array *)*out;
            *R = array(*numel, indices, afHost).as(f32);
            vec_add((void *)out);
        } catch (af::exception& ex) {
            *err = 12;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_join_(int *dim, void **out, void **in1, void **in2, int *err)
    {
        try {
            *out = (void *)new array();
            array *R = (array *)*out;
            array *F = (array *)*in1;
            array *S = (array *)*in2;
            *R = join(*dim -1, *F, *S);
            vec_add((void *)out, *in1, *in2);
        } catch (af::exception& ex) {
            *err = 12;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void init_post_(void **in, int *shape, int *rank)
    {
        try {
            array *R = (array *)*in;
            for (int i = 0; i < 4; i++) shape[i] = R->dims(i);
            *rank = R->numdims();
        } catch (af::exception& ex) {
            printf("%s\n", ex.what());
            exit(-1);
        }
    }

    void af_arr_print_(void **ptr, int *err)
    {
        try {
            array *tmp = (array *)*ptr;
            _print(NULL, *tmp);
        } catch (af::exception& ex) {
            *err = 4;
            printf("%s\n", ex.what());
            exit(-1);
        }
    }
}
