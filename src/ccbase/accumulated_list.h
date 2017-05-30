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
#ifndef CCBASE_ACCUMULATED_LIST_H_
#define CCBASE_ACCUMULATED_LIST_H_

#include <atomic>
#include <memory>
#include <utility>
#include "ccbase/common.h"

namespace ccb {

template <class T>
class AccumulatedList {
 public:
  AccumulatedList()
      : head_(nullptr) {}
  ~AccumulatedList();

  T* AddNode();
  template <class F> void Travel(F&& f);
  template <class F> T* FindNode(F&& f);

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
  for (Node* node = head_.load(std::memory_order_seq_cst);
       node != nullptr; node = node->next) {
    f(&node->data);
  }
}

template <class T>
template <class F>
T* AccumulatedList<T>::FindNode(F&& f) {
  for (Node* node = head_.load(std::memory_order_seq_cst);
       node != nullptr; node = node->next) {
    if (f(&node->data))
      return &node->data;
  }
  return nullptr;
}

template <class T>
AccumulatedList<T>::~AccumulatedList() {
  for (Node* node = head_.load(std::memory_order_seq_cst);
       node != nullptr; ) {
    Node* next = node->next;
    delete node;
    node = next;
  }
}

template <class T>
class AllocatedList {
 public:
  AllocatedList() {}
  ~AllocatedList() {}

  T* Alloc();
  void Free(T* ptr);
  /* task care of concurrency issues here and it's caller's responsablity
   * for thread-safety
   */
  template <class F> void Travel(F&& f);

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(AllocatedList);

  struct Node {
    std::atomic<bool> is_allocated;
    T data;
    Node() : is_allocated(true), data() {}
  };
  AccumulatedList<Node> list_;
};

template <class T>
T* AllocatedList<T>::Alloc() {
  Node* node = list_.FindNode([](Node* node) {
    if (!node->is_allocated.load(std::memory_order_relaxed)) {
      bool old_value = false;
      if (node->is_allocated.compare_exchange_weak(
                                 old_value, true,
                                 std::memory_order_release,
                                 std::memory_order_relaxed)) {
        new (&node->data) T();
        return true;
      }
    }
    return false;
  });
  if (!node)
    node = list_.AddNode();
  return &node->data;
}

template <class T>
void AllocatedList<T>::Free(T* ptr) {
  Node* node = reinterpret_cast<Node*>(reinterpret_cast<char*>(ptr)
                                       - offsetof(Node, data));
  node->data.~T();
  node->is_allocated.store(false, std::memory_order_release);
}

template <class T>
template <class F>
void AllocatedList<T>::Travel(F&& f) {
  list_.Travel([&f](Node* node) {
    if (node->is_allocated.load(std::memory_order_acquire)) {
      f(&node->data);
    }
  });
}

template <class T, class ScopeT = T>
class ThreadLocalList {
 public:
  T* LocalNode();
  template <class F> void Travel(F&& f);

 private:
  static void FreeLocalNode(T* ptr);
  static AllocatedList<T>* GlobalList();
  static std::shared_ptr<AllocatedList<T>> CreateGlobalListOnce();
};

template <class T, class ScopeT>
T* ThreadLocalList<T, ScopeT>::LocalNode() {
  static thread_local std::unique_ptr<T, decltype(&FreeLocalNode)>
      tls_local_node{GlobalList()->Alloc(), &FreeLocalNode};
  return tls_local_node.get();
}

template <class T, class ScopeT>
void ThreadLocalList<T, ScopeT>::FreeLocalNode(T* ptr) {
  GlobalList()->Free(ptr);
}

template <class T, class ScopeT>
template <class F>
void ThreadLocalList<T, ScopeT>::Travel(F&& f) {
  GlobalList()->Travel(std::forward<F>(f));
}

template <class T, class ScopeT>
AllocatedList<T>* ThreadLocalList<T, ScopeT>::GlobalList() {
  static thread_local std::shared_ptr<AllocatedList<T>>
      tls_global_list{CreateGlobalListOnce()};
  return tls_global_list.get();
}

template <class T, class ScopeT>
std::shared_ptr<AllocatedList<T>>
ThreadLocalList<T, ScopeT>::CreateGlobalListOnce() {
  // a safe assumption: only called before return of main()
  static std::shared_ptr<AllocatedList<T>> g_list{new AllocatedList<T>()};
  return g_list;
}

}  // namespace ccb

#endif  // CCBASE_ACCUMULATED_LIST_H_
