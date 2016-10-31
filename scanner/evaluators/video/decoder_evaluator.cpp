/* Copyright 2016 Carnegie Mellon University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "scanner/evaluators/video/decoder_evaluator.h"

#include "scanner/util/memory.h"

namespace scanner {

DecoderEvaluator::DecoderEvaluator(const EvaluatorConfig &config,
                                   DeviceType device_type,
                                   VideoDecoderType decoder_type)
    : device_type_(device_type), device_id_(config.device_ids[0]),
      decoder_type_(decoder_type), discontinuity_(false) {
  decoder_.reset(VideoDecoder::make_from_config(device_type_, device_id_,
                                                decoder_type_, device_type_));
  assert(decoder_.get());
}

void DecoderEvaluator::configure(const VideoMetadata& metadata) {
  metadata_ = metadata;
  frame_size_ = metadata_.width() * metadata_.height() * 3;
  decoder_->configure(metadata);
}

void DecoderEvaluator::reset() {
  discontinuity_ = true;
}

void DecoderEvaluator::evaluate(
  const std::vector<std::vector<u8*>>& input_buffers,
  const std::vector<std::vector<size_t>>& input_sizes,
  std::vector<std::vector<u8*>>& output_buffers,
  std::vector<std::vector<size_t>>& output_sizes)
{
  auto start = now();

  DecodeArgs& args = *reinterpret_cast<DecodeArgs*>(input_buffers[1][0]);
  assert(input_sizes[1][0] == sizeof(DecodeArgs));

  const u8* encoded_buffer = input_buffers[0][0];
  size_t encoded_buffer_size = input_sizes[0][0];

  i32 discard_until_frame =
      discontinuity_ ? args.warmup_start_frame : args.start_frame;
  i32 total_output_frames = args.end_frame - discard_until_frame;

  size_t encoded_buffer_offset = 0;
  i32 current_frame = args.start_keyframe;
  while (current_frame < args.end_frame) {
    auto video_start = now();

    i32 encoded_packet_size = 0;
    const u8 *encoded_packet = NULL;
    if (encoded_buffer_offset < encoded_buffer_size) {
      encoded_packet_size =
          *reinterpret_cast<const i32*>(encoded_buffer + encoded_buffer_offset);
      encoded_buffer_offset += sizeof(i32);
      encoded_packet = encoded_buffer + encoded_buffer_offset;
      encoded_buffer_offset += encoded_packet_size;
    }

    if (decoder_->feed(encoded_packet, encoded_packet_size, discontinuity_)) {
      // New frames
      bool more_frames = true;
      while (more_frames && current_frame < args.end_frame) {
        if (current_frame >= discard_until_frame) {
          u8* decoded_buffer =
              new_buffer(device_type_, device_id_, frame_size_);
          more_frames = decoder_->get_frame(decoded_buffer, frame_size_);
          output_buffers[0].push_back(decoded_buffer);
          output_sizes[0].push_back(frame_size_);
        } else {
          more_frames = decoder_->discard_frame();
        }
        current_frame++;
      }
    }
    discontinuity_ = (encoded_packet_size == 0);
  }
  // Wait on all memcpys from frames to be done
  decoder_->wait_until_frames_copied();

  if (decoder_->decoded_frames_buffered() > 0) {
    while (decoder_->discard_frame()) {
    };
  }

  if (profiler_) {
    profiler_->add_interval("decode", start, now());
  }
}

DecoderEvaluatorFactory::DecoderEvaluatorFactory(DeviceType device_type,
                                                 VideoDecoderType decoder_type)
    : device_type_(device_type), decoder_type_(decoder_type) {}

EvaluatorCapabilities DecoderEvaluatorFactory::get_capabilities() {
  EvaluatorCapabilities caps;
  caps.device_type = device_type_;
  caps.max_devices = 1;
  caps.warmup_size = 0;
  caps.can_overlap = true;
  return caps;
}

std::vector<std::string> DecoderEvaluatorFactory::get_output_names() {
  return {"frame"};
}

Evaluator*
DecoderEvaluatorFactory::new_evaluator(const EvaluatorConfig& config) {
  return new DecoderEvaluator(config, device_type_, decoder_type_);
}
}
