![logo](./docs/images/logo.png)

<h3 align="center">
<p>A High Performance Inference Library for Sequence Processing and Generation
</h3>


LightSeq is a high performance inference library for sequence processing and generation implemented
in CUDA. 
It enables highly efficient computation of modern NLP models such as **BERT**, **GPT2**,
**Transformer**, etc. 
It is therefore best useful for *Machine Translation*, *Text Generation*, *Dialog*， *Language
Modelling*, and other related tasks using these models. 

The library is built on top of CUDA official
library([cuBLAS](https://docs.nvidia.com/cuda/cublas/index.html),
[Thrust](https://docs.nvidia.com/cuda/thrust/index.html), [CUB](http://nvlabs.github.io/cub/)) and
custom kernel functions which are specially fused and optimized for these widely used models. In
addition to model components, we also provide codes
manage model weights trained from deepleanring framework and servers as a custom backend for
[TensorRT Inference
Server](https://docs.nvidia.com/deeplearning/sdk/inference-server-archived/tensorrt_inference_server_120/tensorrt-inference-server-guide/docs/quickstart.html)(referred
to as TRTIS in the later discussion). With LightSeq, you can easily deploy efficient model services or develop
your own model architectures just with a little code modification.


## Features
- Comprehensive sequence modeling support, including Bert, GPT, Transformer and their VAE variants.
- Various search methods, such as beam search, diverse beam search, topp/topk sampling.
- Out-of-the-box rich middlewares for model service based on TRTIS, such as dynamic batch,
  multi-model on single GPU.
- State of art inference performance compared with Deeplearning framework and other inference
  libraries.

The following is a support matrix of LightSeq compared with
[TurboTransformers](https://github.com/Tencent/TurboTransformers) and
[FasterTransformer](https://github.com/NVIDIA/DeepLearningExamples/tree/master/FasterTransformer).

<img src="./docs/images/support.png" width="400" div align="center"/>

## Performance
Here, we show our experimental results on neural machine translation and text generation. 
The models of these two tasks are Transformer-base, but use beam search and sampling search methods
respectively.
We choose Tensorflow and
[FasterTransformer](https://github.com/NVIDIA/DeepLearningExamples/tree/master/FasterTransformer) as a comparison.
The implementation from
[tensor2tensor](https://github.com/tensorflow/tensor2tensor/blob/master/tensor2tensor/models/transformer.py)
was used as the benchmark of Tensorflow. 

More results is available [here](./docs/performance.md).

* Neural machine translation
<img src="./docs/images/nmt.png" width="400" div align="center"/>

* Text generation
<img src="./docs/images/generation.png" width="400" div align="center"/>


## Code Structure
```python
├── BUILD # bazel build file
├── 3rdparty
│   └── cub-1.8.0 # CUB lib
├── kernels # cuda kernel function
│   ├── common.h  # common function
│   ├── gptKernels.cu.cc # kernel function needed by gpt
│   ├── gptKernels.h
│   ├── transformerKernels.cu.cc # kernel function needed by transformer
│   └── transformerKernels.h
├── model # model infer component
│   ├── decoder.cu.cc # transformer decoder
│   ├── decoder.h 
│   ├── encoder.cu.cc # transformer encoder
│   ├── encoder.h
│   ├── gpt_encoder.cu.cc # gpt
│   └── gpt_encoder.h
├── proto # proto for model weights
│   ├── gpt.proto
│   ├── gpt_weight.cu.cc # model weights loader
│   ├── gpt_weight.h
│   ├── transformer.proto
│   ├── transformer_weight.cu.cc # model weights loader
│   └── transformer_weight.h
├── example # local inference demo
│   ├── gptlm_example.cu.cc # gptlm demo
│   ├── gpt_generation.cu.cc # gpt generation demo
│   └── transformer_example.cu.cc # transformer demo
├── server # model inference server based on TRTIS
│   ├── generate_server.cu.cc # transfomer genearate server, multi-target for one source
│   ├── gptlm_server.cu.cc # gptlm server
│   ├── gpt_generate_server.cu.cc # gpt generation server
│   └── transformer_server.cu.cc # transfomer server, one target for one source
└── tools # development tools. e.g. runtime guard, debug
```
## Requirements
- Install Docker and [nvidia-docker](https://github.com/NVIDIA/nvidia-docker).
- GPU driver version >= 410.48
- [Login to the NGC registry](https://docs.nvidia.com/ngc/ngc-getting-started-guide/index.html).

## Quick Start
To avoid problems caused by inconsistent environments, you can use the pre-built TRTIS container from
[NVIDIA GPU Cloud (NGC)](https://ngc.nvidia.com/). To start the given container, you need to install
[nvidia-docker](https://github.com/NVIDIA/nvidia-docker) and make your GPU driver version >= 410.48
```shell
docker pull nvcr.io/nvidia/tensorrtserver:19.05-py3
# 
docker run --gpus '"device=0"' -it --rm -p8000:8000 -p8001:8001 -p8002:8002 -v
/${current}/${path}:/quick_start nvcr.io/nvidia/tensorrtserver:19.05-py3 /bin/bash
# inside container
cd /quick_start
```
### Use our pre-build lib
To quickly deploy your model that supported by LightSeq currently, you can download the pre-built libraries
from the GitHub release page corresponding to the release version you are interested in. In each release
version, we will upload binary executable example and dynamic link library of models which is a
custom backend of TRTIS.
```shell
wget https://github.com/bytedance/lightseq/releases/download/${VERSION}/${VERSION}_libs.tar.gz
tar -zxvf ${VERSION}_libs.tar.gz
```
### Run local inference demo
To run local inference demo, you need to prepare model weights saved in custom proto defined by
LightSeq and input token ids. We provide a GPT-LM model and its corresponding input token ids:
```shell
wget https://github.com/bytedance/lightseq/releases/download/v0.0.1/v0.0.1_gptlm.pkg.tar.gz
tar -zxvf v0.0.1_gptlm.pkg.tar.gz
# fp32 example
./{VERSION}_libs/gptlm_example.fp32 ./v0.0.1_gptlm.pkg/gpt.pb ./v0.0.1_gptlm.pkg/test_case
# fp16 example
./{VERSION}_libs/gptlm_example.fp16 ./v0.0.1_gptlm.pkg/gpt.pb ./v0.0.1_gptlm.pkg/test_case
```

### Run inference server
To run the end-to-end model server based on TRTIS, you need to prepare a custom backend [model
repository](https://docs.nvidia.com/deeplearning/sdk/inference-server-archived/tensorrt_inference_server_120/tensorrt-inference-server-guide/docs/model_repository.html#custom-backends) like this:
```shell
models/
  <model-name>/
    config.pbtxt # configuration
    xxx # model weights
    1/
      libyyy.so # custom dynamic link library
```
With the pre-built libraries and example weights mentioned above, you can easily run a server:
```shell
mkdir -p ./model_zoo/gptlm/1
wget https://github.com/bytedance/lightseq/releases/download/v0.0.1/v0.0.1_gptlm.config.pbtxt
mv v0.0.1_gptlm.config.pbtxt model_zoo/gptlm/config.pbtxt
cp ./v0.0.1_gptlm.pkg/gpt.pb model_zoo/gptlm/gpt.pb
cp ./{VERSION}_libs/libgptlm.so.fp32 model_zoo/gptlm/1/libgptlm.so
# or fp16 server
# cp ./{VERSION}_libs/libgptlm.so.fp16 model_zoo/gptlm/1/libgptlm.so
export MODEL_ZOO="/quick_start/model_zoo"
trtserver --model-store=${MODEL_ZOO}
```
After starting server, Invoking the [TRTIS
client](https://docs.nvidia.com/deeplearning/sdk/inference-server-archived/tensorrt_inference_server_120/tensorrt-inference-server-guide/docs/client.html) will get the inference result.

### Serve your own model
In order to serve your own model, you need to [export model](./docs/export_model.md) trained from deeplearning framework(E.g.
TenforFlow, PyTorch) to custom model proto defined by LightSeq. Furthermore, you may need to [build from
source code](./docs/build.md) if you want to modify the model architectures or serve a new model not supported by
LightSeq currently.

## Limitations and Future Plans
LightSeq does not support CPU inference for now and its compilation relies heavily on TRTIS, we will
try to solve these problems in future. Furthermore, the following will be the focus of our future
work:
- Support more model architectures and decoding search algorithms.
- Int8 inference.
- Device deployment.

## Contact
Any questions or suggestions, please feel free to contact us with wangxiaohui.neo@bytedance.com
