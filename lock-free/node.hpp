#ifndef NODE_HPP_INCLUDED
#define NODE_HPP_INCLUDED

namespace iftracer {
template <typename T>
class Node {
 public:
  Node* volatile next;
  Node(T value) : value_(value) {}
  T value_;

 public:
  Node() : next(nullptr){};
};
}  // namespace iftracer

#endif  // NODE_HPP_INCLUDED
