#include <algorithm>

#include "src/custom/byseqlib/model/gpt_encoder.h"
#include "src/custom/byseqlib/proto/gpt_weight.h"
#include "src/custom/byseqlib/tools/util.h"

/**
@file
Example of how to run gpt inference using our implementation.
*/

// Appoint precision.
const byseqlib::cuda::OperationType optype =
    byseqlib::cuda::OperationType::FP16;

int main(int argc, char *argv[]) {
  /* ---step1. init environment--- */
  cudaStream_t stream_;
  cublasHandle_t hd_;
  cudaSetDevice(0);
  cudaStreamCreate(&stream_);
  cublasCreate(&hd_);
  cublasSetStream(hd_, stream_);

  /* ---step2. load model weights into GPU memory--- */
  byseqlib::cuda::GptWeight<optype> tw_;
  // saved in custom proto file
  std::string model_weights_path = argv[1];
  std::string res = tw_.initializing(model_weights_path);
  if (!res.empty()) {
    std::cout << res << std::endl;
    return 0;
  }

  /*
    step3. instantiate encoder, init the gpu memory buffer.
      using thrust vector to avoid manage gpu memory by hand
  */
  int max_batch_size = 128;
  thrust::device_vector<int> d_input_ =
      std::vector<int>(max_batch_size * tw_._max_step, 0);
  thrust::device_vector<float> d_ppl_ = std::vector<float>(max_batch_size, 0.f);
  std::shared_ptr<byseqlib::cuda::GptEncoder<optype>> encoder_ =
      std::make_shared<byseqlib::cuda::GptEncoder<optype>>(
          max_batch_size,
          reinterpret_cast<int *>(thrust::raw_pointer_cast(d_input_.data())),
          reinterpret_cast<float *>(thrust::raw_pointer_cast(d_ppl_.data())),
          tw_, stream_, hd_);
  res = encoder_->check();
  if (!res.empty()) {
    std::cout << res << std::endl;
    return 1;
  }
  // init gpu memory buffer
  int buf_bytesize = encoder_->compute_buffer_bytesize();
  thrust::device_vector<int> d_buf_ =
      std::vector<int>(buf_bytesize / sizeof(int) + 1, 0);
  encoder_->init_buffer(
      reinterpret_cast<void *>(thrust::raw_pointer_cast(d_buf_.data())));
  cudaStreamSynchronize(stream_);

  /* ---step4. read input token ids from file--- */
  int batch_size;
  int batch_seq_len;
  std::vector<int> host_input;
  // the first line of input file should
  // be two integers: batch_size and batch_seq_len.
  // followed by batch_size lines of
  // batch_seq_len integers, e.g.
  // 2 3
  // 666 666 666
  // 666 666 666
  std::string input_file_name = argv[2];
  byseqlib::cuda::read_batch_tokenids_from_file(input_file_name, batch_size,
                                                batch_seq_len, host_input);

  /* ---step5. infer and log--- */
  for (int i = 0; i < 10; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    // copy inputs from cpu memory to gpu memory
    cudaMemcpyAsync(
        reinterpret_cast<int *>(thrust::raw_pointer_cast(d_input_.data())),
        host_input.data(), sizeof(int) * batch_size * batch_seq_len,
        cudaMemcpyHostToDevice, stream_);
    encoder_->run_one_infer(batch_size, batch_seq_len);
    byseqlib::cuda::print_time_duration(start, "one infer time", stream_);
    byseqlib::cuda::print_vec(d_ppl_.data(), "ppl", batch_size);
  }
  return 0;
}
