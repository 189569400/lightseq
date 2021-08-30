#include <sys/time.h>
#include <cuda_profiler_api.h>
#include <cublasLt.h>
#include <cuda.h>
#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <stdio.h>
#include <algorithm>
#include <vector>

int8_t float2int8(float f, float scale) {
  int8_t i = int8_t(f * scale);
  if (i < -127) i = -127;
  if (i > 127) i = 127;
  return i;
}

void matmul(float *A, float *B, float *C, int m, int n, int k) {
  for (int i = 0; i < m; ++i)
    for (int j = 0; j < n; ++j) {
      C[i * n + j] = 0;
      for (int kk = 0; kk < k; ++kk)
        C[i * n + j] += A[i * k + kk] * B[j * k + kk];
    }
}

template <typename T, typename S>
void allocate_memory(int B, int O, int H, T **X, T **W, S **Y) {
  cudaMallocManaged(X, B * H * sizeof(T));
  cudaMallocManaged(W, O * H * sizeof(T));
  cudaMallocManaged(Y, B * O * sizeof(S));
}

template <typename T, typename S>
void free_memory(T *X, T *W, S *Y) {
  cudaFree(X);
  cudaFree(W);
  cudaFree(Y);
}

template <typename T, typename S>
int cublas_lt_matmul(cublasLtHandle_t handle, cublasLtMatmulDesc_t matmulDesc,
                     cublasLtMatrixLayout_t XDesc, cublasLtMatrixLayout_t WDesc,
                     cublasLtMatrixLayout_t YDesc, T *A, T *B, S *C, S *alpha,
                     S *beta) {
  cublasStatus_t status;
  status = cublasLtMatmul(handle, matmulDesc, alpha, A, XDesc, B, WDesc, beta,
                          C, YDesc, C, YDesc, nullptr, nullptr, 0, 0);

  if (status == CUBLAS_STATUS_SUCCESS)
    return 1;
  else {
    return -1;
  }
}

template <typename T, typename S>
void test_lt_matmul(cublasLtHandle_t handle, int B, int O, int H, T *X, T *W,
                    S *Y, S *alpha, S *beta, int iteration) {
  cudaDataType_t XType, WType, YType;
#if CUBLAS_VER_MAJOR == 11
  cublasComputeType_t ComputeType;
  cudaDataType_t scaleType;
#else
  cudaDataType_t ComputeType;
#endif
  if (std::is_same<T, float>::value) {
    XType = WType = YType = CUDA_R_32F;
#if CUBLAS_VER_MAJOR == 11
    ComputeType = CUBLAS_COMPUTE_32F;
    scaleType = CUDA_R_32F;
#else
    ComputeType = CUDA_R_32F;
#endif
  } else if (std::is_same<T, __half>::value) {
    XType = WType = YType = CUDA_R_16F;
#if CUBLAS_VER_MAJOR == 11
    ComputeType = CUBLAS_COMPUTE_16F;
    scaleType = CUDA_R_16F;
#else
    ComputeType = CUDA_R_16F;
#endif
  } else {
    printf("Not supported data type.");
    return;
  }

  cublasOperation_t opTrans = CUBLAS_OP_T;

  cublasLtMatrixLayout_t XDesc, WDesc, YDesc;
  cublasLtMatrixLayoutCreate(&XDesc, XType, H, B, H);
  cublasLtMatrixLayoutCreate(&WDesc, WType, H, O, H);
  cublasLtMatrixLayoutCreate(&YDesc, YType, O, B, O);

  T *Wtransform;
  cudaMalloc(reinterpret_cast<void **>(&Wtransform), sizeof(T) * O * H);

  cublasLtMatrixLayout_t WtransformDesc;
  cublasLtMatrixLayoutCreate(&WtransformDesc, WType, O, H, O);

  cublasLtMatrixTransformDesc_t transformDesc;
  cublasLtMatrixTransformDescCreate(&transformDesc, CUDA_R_32F);
  cublasLtMatrixTransformDescSetAttribute(transformDesc,
                                          CUBLASLT_MATRIX_TRANSFORM_DESC_TRANSA,
                                          &opTrans, sizeof(opTrans));

  float transformAlpha = 1.0f, transformBeta = 0.0f;
  cublasLtMatrixTransform(handle, transformDesc, &transformAlpha, W, WDesc,
                          &transformBeta, NULL, NULL, Wtransform,
                          WtransformDesc, 0);

  cublasLtMatmulDesc_t matmulDesc;
#if CUBLAS_VER_MAJOR == 11
  cublasLtMatmulDescCreate(&matmulDesc, ComputeType, scaleType);
#else
  cublasLtMatmulDescCreate(&matmulDesc, ComputeType);
#endif

  float total_time = 0;
  for (int i = 0; i < iteration; ++i) {
    cudaEvent_t start, stop;
    float time;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    cudaEventRecord(start, 0);
    int success = cublas_lt_matmul(handle, matmulDesc, WtransformDesc, XDesc,
                                   YDesc, Wtransform, X, Y, alpha, beta);
    cudaEventRecord(stop, 0);
    cudaEventSynchronize(stop);

    cudaEventElapsedTime(&time, start, stop);
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    if (success > 0 && i >= 10) total_time += time;
  }
  if (total_time > 0) printf("%.3f ms\n", total_time / (iteration - 10));

  cublasLtMatrixLayoutDestroy(WtransformDesc);
  cublasLtMatrixLayoutDestroy(XDesc);
  cublasLtMatrixLayoutDestroy(WDesc);
  cublasLtMatrixLayoutDestroy(YDesc);
  cublasLtMatmulDescDestroy(matmulDesc);
  cublasLtMatrixTransformDescDestroy(transformDesc);
  cudaDeviceSynchronize();
  cudaFree(Wtransform);
}

void test_lt_matmul_int8(cublasLtHandle_t handle, int B, int O, int H,
                         int8_t *X, int8_t *W, int32_t *Y, int32_t *alpha,
                         int32_t *beta, int iteration) {
#if CUBLAS_VER_MAJOR == 11
  cublasComputeType_t ComputeType = CUBLAS_COMPUTE_32I;
  cudaDataType_t scaleType = CUDA_R_32I;
#else
  cudaDataType_t ComputeType = CUDA_R_32I;
#endif
  cudaDataType_t XType, WType, YType;
  XType = WType = CUDA_R_8I;
  YType = CUDA_R_32I;

  cublasOperation_t opTrans = CUBLAS_OP_T;
  cublasLtOrder_t order_COL32 = CUBLASLT_ORDER_COL32;
  cublasLtOrder_t order_COL4_4R2_8C = CUBLASLT_ORDER_COL4_4R2_8C;

  cublasLtMatrixLayout_t XDesc, WDesc, YDesc;
  cublasLtMatrixLayoutCreate(&XDesc, XType, H, B, H);
  cublasLtMatrixLayoutCreate(&WDesc, WType, H, O, H);
  cublasLtMatrixLayoutCreate(&YDesc, YType, O, B, O);

  int8_t *Xtransform, *Wtransform;
  int32_t *Ytransform;
  cudaMalloc(reinterpret_cast<void **>(&Xtransform), sizeof(int8_t) * B * H);
  cudaMalloc(reinterpret_cast<void **>(&Wtransform), sizeof(int8_t) * O * H);
  cudaMalloc(reinterpret_cast<void **>(&Ytransform), sizeof(int32_t) * B * O);

  int ldXtransform = 32 * B;
  int ldWtransform = 32 * O;
  int ldYtransform = 32 * B;
  cublasLtMatrixLayout_t XtransformDesc, WtransformDesc, YtransformDesc;
  cublasLtMatrixLayoutCreate(&XtransformDesc, CUDA_R_8I, B, H, ldXtransform);
  cublasLtMatrixLayoutSetAttribute(XtransformDesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                   &order_COL32, sizeof(order_COL32));
  cublasLtMatrixLayoutCreate(&WtransformDesc, CUDA_R_8I, O, H, ldWtransform);
  cublasLtMatrixLayoutSetAttribute(WtransformDesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                   &order_COL4_4R2_8C,
                                   sizeof(order_COL4_4R2_8C));
  cublasLtMatrixLayoutCreate(&YtransformDesc, CUDA_R_32I, B, O, ldYtransform);
  cublasLtMatrixLayoutSetAttribute(YtransformDesc, CUBLASLT_MATRIX_LAYOUT_ORDER,
                                   &order_COL32, sizeof(order_COL32));

  cublasLtMatrixTransformDesc_t transformDesc;
  cublasLtMatrixTransformDescCreate(&transformDesc, CUDA_R_32F);
  cublasLtMatrixTransformDescSetAttribute(transformDesc,
                                          CUBLASLT_MATRIX_TRANSFORM_DESC_TRANSA,
                                          &opTrans, sizeof(opTrans));

  float transformAlpha = 1.0f, transformBeta = 0.0f;
  cublasLtMatrixTransform(handle, transformDesc, &transformAlpha, X, XDesc,
                          &transformBeta, NULL, NULL, Xtransform,
                          XtransformDesc, 0);
  cublasLtMatrixTransform(handle, transformDesc, &transformAlpha, W, WDesc,
                          &transformBeta, NULL, NULL, Wtransform,
                          WtransformDesc, 0);

  cublasLtMatmulDesc_t matmulDesc;
#if CUBLAS_VER_MAJOR == 11
  cublasLtMatmulDescCreate(&matmulDesc, ComputeType, scaleType);
#else
  cublasLtMatmulDescCreate(&matmulDesc, ComputeType);
#endif
  cublasLtMatmulDescSetAttribute(matmulDesc, CUBLASLT_MATMUL_DESC_TRANSB,
                                 &opTrans, sizeof(opTrans));

  float total_time = 0;
  for (int i = 0; i < iteration; ++i) {
    cudaEvent_t start, stop;
    float time;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    cudaEventRecord(start, 0);
    int success = cublas_lt_matmul(handle, matmulDesc, XtransformDesc,
                                   WtransformDesc, YtransformDesc, Xtransform,
                                   Wtransform, Ytransform, alpha, beta);
    cudaEventRecord(stop, 0);
    cudaEventSynchronize(stop);

    cudaEventElapsedTime(&time, start, stop);
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    if (success > 0 && i >= 10) total_time += time;
  }
  if (total_time > 0) printf("%.3f ms\n", total_time / (iteration - 10));
  cublasLtMatrixTransformDescSetAttribute(transformDesc,
                                          CUBLASLT_MATRIX_TRANSFORM_DESC_TRANSA,
                                          &opTrans, sizeof(opTrans));
  cublasLtMatrixTransform(handle, transformDesc, &transformAlpha, Ytransform,
                          YtransformDesc, &transformBeta, NULL, NULL, Y, YDesc,
                          0);

  cublasLtMatrixLayoutDestroy(XtransformDesc);
  cublasLtMatrixLayoutDestroy(WtransformDesc);
  cublasLtMatrixLayoutDestroy(YtransformDesc);
  cublasLtMatrixLayoutDestroy(XDesc);
  cublasLtMatrixLayoutDestroy(WDesc);
  cublasLtMatrixLayoutDestroy(YDesc);
  cublasLtMatmulDescDestroy(matmulDesc);
  cublasLtMatrixTransformDescDestroy(transformDesc);
  cudaDeviceSynchronize();
  cudaFree(Xtransform);
  cudaFree(Wtransform);
  cudaFree(Ytransform);
}

void _main(int B, int O, int H, int iteration, bool debug) {
  printf(
      ">>>>>>>>>>>>>>>>>>>> shape: X(%d, %d), W(%d, %d) >>>>>>>>>>>>>>>>>>>>\n",
      B, H, O, H);

  float *Y;
  if (debug) cudaMallocManaged(&Y, B * O * sizeof(float));

  float *fX, *fW, *fY;
  __half *hX, *hW, *hY;
  int8_t *iX, *iW;
  int32_t *iY;
  allocate_memory(B, O, H, &fX, &fW, &fY);
  allocate_memory(B, O, H, &hX, &hW, &hY);
  allocate_memory(B, O, H, &iX, &iW, &iY);

  float f_alpha = 1, f_beta = 0;
  __half h_alpha = __float2half_rn(1.0), h_beta = __float2half_rn(0.0);
  int32_t i_alpha = 1, i_beta = 0;

  for (int i = 0; i < B * H; ++i) {
    fX[i] = float(i % 255 - 127) / 127;
    hX[i] = __float2half_rn(fX[i]);
    iX[i] = float2int8(fX[i], 127);
  }
  for (int i = 0; i < O * H; ++i) {
    fW[i] = float(i % 255 - 127) / 127;
    hW[i] = __float2half_rn(fW[i]);
    iW[i] = float2int8(fW[i], 127);
  }

  if (debug) matmul(fX, fW, Y, B, O, H);

  cublasLtHandle_t handle;
  cublasLtCreate(&handle);

  printf(">>>>> test fp32 >>>>>\n");
  test_lt_matmul(handle, B, O, H, fX, fW, fY, &f_alpha, &f_beta, iteration);

  printf(">>>>> test fp16 >>>>>\n");
  test_lt_matmul(handle, B, O, H, hX, hW, hY, &h_alpha, &h_beta, iteration);

  printf(">>>>> test int8 >>>>>\n");
  test_lt_matmul_int8(handle, B, O, H, iX, iW, iY, &i_alpha, &i_beta,
                      iteration);

  float fe = 0, he = 0, ie = 0;
  printf(">>>>> compare result >>>>>\n");
  if (debug) {
    printf("oracle:\n  ");
    for (int i = 0; i < 10; ++i) printf("%.5f%c", Y[i], " \n"[i == 9]);
  }

  printf("fp32:\n  ");
  for (int i = 0; i < 10; ++i) printf("%.5f%c", fY[i], " \n"[i == 9]);
  for (int i = 0; i < B * O; ++i) fe += fabs((debug ? Y[i] : fY[i]) - fY[i]);
  printf("  diff: %.5f\n", fe / B / O);

  printf("fp16:\n  ");
  for (int i = 0; i < 10; ++i) printf("%.5f%c", float(hY[i]), " \n"[i == 9]);
  for (int i = 0; i < B * O; ++i)
    he += fabs((debug ? Y[i] : fY[i]) - float(hY[i]));
  printf("  diff: %.5f\n", he / B / O);

  printf("int8:\n  ");
  for (int i = 0; i < 10; ++i)
    printf("%.5f%c", float(iY[i]) / 127 / 127, " \n"[i == 9]);
  for (int i = 0; i < B * O; ++i)
    ie += fabs((debug ? Y[i] : fY[i]) - float(iY[i]) / 127 / 127);
  printf("  diff: %.5f\n", ie / B / O);

  free_memory(fX, fW, fY);
  free_memory(hX, hW, hY);
  free_memory(iX, iW, iY);
  if (debug) cudaFree(Y);
}

int main() {
  int iteration = 50;
  bool debug = false;
  std::vector<int> Bs = {8, 16, 4096};
  std::vector<int> Os = {1024, 3072, 4096};
  std::vector<int> Hs = {1024, 4096};
  for (int i = 0; i < Bs.size(); ++i) {
    for (int j = 0; j < Os.size(); ++j) {
      for (int k = 0; k < Hs.size(); ++k) {
        _main(Bs[i], Os[j], Hs[k], iteration, debug);
      }
    }
  }
  return 0;
}
