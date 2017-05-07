/* Copyright (c) 2016-2017, Bin Wei <bin@vip.qq.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * The names of its contributors may not be used to endorse or 
 * promote products derived from this software without specific prior 
 * written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef ACCUMULATED_LIST_H_
#define ACCUMULATED_LIST_H_

#include <atomic>
#include <utility>
#include "ccbase/common.h"

namespace ccb {

template <class T>
class AccumulatedList {
 public:
  AccumulatedList()
      : head_(nullptr) {}
  ~AccumulatedList() {}  // allow memory leaks here

  T* AddNode();
  template <class F> void Travel(F&& f);

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(AccumulatedList);

  struct Node {
    T     data;
    Node* next;
    Node() : data(), next(nullptr) {}
  };

  std::atomic<Node*> head_;
};

template <class T>
T* AccumulatedList<T>::AddNode() {
  Node* new_node = new Node();
  Node* old_head = head_.load(std::memory_order_relaxed);
  do {
    new_node->next = old_head;
  } while (!head_.compare_exchange_weak(old_head, new_node,
                                        std::memory_order_release,
                                        std::memory_order_relaxed));
  return &new_node->data;
}

template <class T>
template <class F>
void AccumulatedList<T>::Travel(F&& f) {
  for (Node* node = head_.load(std::memory_order_relaxed);
       node != nullptr; node = node->next) {
    f(&node->data);
  }
}


template <class T, class ScopeT>
class TlsAccumulatedList {
 public:
  static T* TlsNode();
  template <class F> static void Travel(F&& f);

 private:
  static AccumulatedList<T> cls_acc_list_;
  static thread_local T* tls_node_;
};

template <class T, class ScopeT>
AccumulatedList<T> TlsAccumulatedList<T, ScopeT>::cls_acc_list_;

template <class T, class ScopeT>
thread_local T* TlsAccumulatedList<T, ScopeT>::tls_node_{nullptr};

template <class T, class ScopeT>
T* TlsAccumulatedList<T, ScopeT>::TlsNode() {
  if (!tls_node_) {
    tls_node_ = cls_acc_list_.AddNode();
  }
  return tls_node_;
}

template <class T, class ScopeT>
template <class F>
void TlsAccumulatedList<T, ScopeT>::Travel(F&& f) {
  cls_acc_list_.Travel(std::forward<F>(f));
}

}  // namespace ccb

#endif  // ACCUMULATED_LIST_H_
