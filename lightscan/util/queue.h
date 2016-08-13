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

#pragma once

#include <mutex>
#include <deque>
#include <condition_variable>
#include <atomic>

namespace lightscan {

template <typename T>
class Queue {
public:
  Queue();

  int size();

  template <typename... Args>
  void emplace(Args&&... args);

  void push(T item);

  bool try_pop(T& item);

  void pop(T& item);

private:
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<T> data_;
  std::atomic<int> waiters_;
};

}

#include "queue.inl"
