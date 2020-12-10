#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include "pywrapper/transformer.cc.cu"
#include "pywrapper/transformer_decoder.cc.cu"

namespace py = pybind11;

PYBIND11_MODULE(lightseq, m) {
  py::class_<lightseq::cuda::TransformerDecoder>(m, "TransformerDecoder")
      .def(py::init<const std::string, const int>())
      .def("infer", &lightseq::cuda::TransformerDecoder::infer);

  py::class_<lightseq::cuda::Transformer>(m, "Transformer")
      .def(py::init<const std::string, const int>())
      .def("infer", &lightseq::cuda::Transformer::infer,
           py::return_value_policy::reference_internal, py::arg("input_seq"),
           py::arg("multiple_output") = false, py::arg("sampling_method") = "",
           py::arg("beam_size") = -1, py::arg("length_penalty") = -1.0f,
           py::arg("topp") = -1.0f, py::arg("topk") = -1.0f,
           py::arg("diverse_lambda") = -1.0f);
}
