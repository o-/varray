#include <cstdint>
#include <cassert>
#include <cstring>
#include <string>
#include <stack>
#include <iostream>
#include <array>

#pragma pack(push, 1) // exact fit - no padding

class Node {
 public:
  enum class Type : char {
    Constant,
    Add,
  };

  inline size_t realSize() const;

  const Type type;
  Node(const Type type) : type(type) {}

  virtual void print(std::ostream& os) const = 0;
  friend std::ostream& operator<<(std::ostream& os, const Node& n) {
    n.print(os);
    return os;
  }

  virtual void adjustOffset(Node * n) {
    *(Node**)n = this;
  }
};

class Constant : public Node {
 public:
  const int value;
  Constant(int value) : Node(Type::Constant), value(value) {}

  void print(std::ostream& os) const override {
    os << value;
  }
};


class Add : public Node {
  intptr_t offset_l;
  intptr_t offset_r;

 public:
  Add(Node* l, Node* r) : Node(Type::Add),
                          offset_l((intptr_t)l - (intptr_t)this),
                          offset_r((intptr_t)r - (intptr_t)this) {}

  Node* l() const {
    return (Node*)((intptr_t)this + offset_l);
  }

  Node* r() const {
    return (Node*)((intptr_t)this + offset_r);
  }

  void print(std::ostream& os) const override {
    os << *l() << " + " << *r();
  }

  void adjustOffset(Node* old_) override {
    Add* old = static_cast<Add*>(old_);
    Node * up_r = *(Node**)old->r();
    Node * up_l = *(Node**)old->l();
    offset_l = (intptr_t)up_l - (intptr_t)this;
    offset_r = (intptr_t)up_r - (intptr_t)this;
    Node::adjustOffset(old_);
  }
};

size_t Node::realSize() const {
  switch(type) {
    case Type::Constant: return sizeof(Constant);
    case Type::Add: return sizeof(Add);
  }
  assert(false);
  return -1;
}

#pragma pack(pop) // exact fit - no padding

class NodeList {
  static constexpr size_t defaultSize = 128*1024;
  static constexpr size_t gapSize = sizeof(NodeList*);

  uintptr_t buf;
  uintptr_t pos;

  bool full = false;
  NodeList* nextFree = nullptr;

  const uintptr_t size;

  // Keeps a list of gaps in this NodeList for faster dtr and
  // totalSize(). If the list overflows we go the slow way and
  // find the sublists by inspecting all gaps.
  class GapsCache {
    static constexpr unsigned gapsCacheSize = 8;
    unsigned numGaps = 0;
    std::array<NodeList*, gapsCacheSize> gapsCache;

   public:
    void add(NodeList* l) {
      if (numGaps < gapsCacheSize)
        gapsCache[numGaps] = l;
      numGaps++;
    }

    bool overflow() {
      return numGaps > gapsCacheSize;
    }

    std::array<NodeList*, gapsCacheSize>::iterator begin() {
      return gapsCache.begin();
    }

    std::array<NodeList*, gapsCacheSize>::iterator end() {
      auto it = begin();
      std::advance(it, numGaps);
      return it;
    }
  };

  GapsCache gapsCache;


  NodeList* next = nullptr;

 public:
  size_t totalSize() {
    size_t sum = size;

    if (gapsCache.overflow()) {
      uintptr_t finger = buf;
      while (true) {
        NodeList** gap = reinterpret_cast<NodeList**>(finger);
        if (*gap)
          sum += (*gap)->totalSize();
        finger += gapSize;
        if (finger >= pos)
          break;
        auto n = (Node*)finger;
        finger += n->realSize();
      }
    } else {
      for (auto gap : gapsCache) {
        sum += gap->totalSize();
      }
    }
    if (next)
      sum += next->totalSize();
    return sum;
  }

  NodeList(size_t initSize = defaultSize) : size(initSize) {
    buf = (uintptr_t)new char[initSize];

    *(NodeList**)buf = nullptr;
    pos = buf + gapSize;
  }

  ~NodeList() {
    if (gapsCache.overflow()) {
      uintptr_t finger = buf;
      while (true) {
        NodeList** gap = reinterpret_cast<NodeList**>(finger);
        if (*gap) delete *gap;
        finger += gapSize;
        if (finger >= pos)
          break;
        auto n = (Node*)finger;
        finger += n->realSize();
      }
    } else {
      for (auto gap : gapsCache) {
        delete gap;
      }
    }
    if (next) delete next;
    delete[] (char*)buf;
  }

  template<typename Node>
  Node* insert() {
    return new(prepareInsert(sizeof(Node))) Node();
  }

  template<typename Node, typename Arg1>
  Node* insert(Arg1 arg1) {
    return new(prepareInsert(sizeof(Node))) Node(arg1);
  }

  template<typename Node, typename Arg1, typename Arg2>
  Node* insert(Arg1 arg1, Arg2 arg2) {
    return new(prepareInsert(sizeof(Node))) Node(arg1, arg2);
  }

  inline void* prepareInsert(size_t s) {
    if (nextFree && !nextFree->full) {
      return nextFree->prepareInsert(s);
    }

    uintptr_t next_pos = pos + s + gapSize;
    if (next_pos < buf + size) {
      void* res = (void*)pos;
      pos = next_pos;
      *(NodeList**)(pos - gapSize) = nullptr;
      return res;
    }

    return prepareInsertSlow(s);
  }

  void* prepareInsertSlow(size_t s) {
    full = true;

    NodeList* cur = this;
    while(cur->full && cur->next) {
      cur = cur->next;
    }

    if (cur == this) {
      next = nextFree = new NodeList();
      return next->prepareInsert(s);
    }

    nextFree = cur;
    return nextFree->prepareInsert(s);
  }

  inline Node* insert(Node* n) {
    size_t s = n->realSize();
    memcpy((void*)pos, (void*)n, s);
    auto res = (Node*)pos;
    pos += s;
    *(NodeList**)pos = nullptr;
    pos += gapSize;
    return res;
  }

  NodeList* flatten() {
    auto flat = new NodeList(totalSize());

    // Bulk copy, to avoid doing insert(Node*) for every element
    auto fixup = [](uintptr_t old_start,
                    uintptr_t old_end,
                    uintptr_t new_start) -> uintptr_t {

      Node* last = (Node*) old_end;
      size_t last_size = last->realSize();

      size_t s = old_end - old_start + last_size;
      memcpy((void*)new_start, (void*)old_start, s);

      uintptr_t finger_new = new_start;
      uintptr_t finger_old = old_start;

      while (finger_old <= old_end) {
        Node* old = (Node*)finger_old;
        Node* copy = (Node*)finger_new;
        size_t s = copy->realSize();
        copy->adjustOffset(old);
        *((NodeList**)(finger_new - gapSize)) = nullptr;
        finger_old += s + gapSize;
        finger_new += s + gapSize;
      }

      *((NodeList**)(finger_new - gapSize)) = nullptr;

      return finger_new;
    };

    auto i = begin();
    NodeList* cur = i.cur;
    uintptr_t bulkFixupStart = i.pos;
    uintptr_t bulkFixupEnd = i.pos;
    unsigned depth = 0;

    for (; !i.isEnd(); ++i) {
      if (cur != i.cur || i.worklist.size() != depth) {
        flat->pos = fixup(bulkFixupStart, bulkFixupEnd, flat->pos);
        depth = i.worklist.size();
        cur = i.cur;
        bulkFixupStart = i.pos;
      }
      bulkFixupEnd = i.pos;
    }
    flat->pos = fixup(bulkFixupStart, bulkFixupEnd, flat->pos);
    delete this;

    return flat;
  }

  class iterator {
    NodeList* cur;
    uintptr_t pos;
    uintptr_t end;
    std::stack<uintptr_t> worklist;

    void popWorklist() {
        end = worklist.top();
        worklist.pop();
        pos = worklist.top();
        worklist.pop();
    }

    inline void findStart() {
      while (true) {
        NodeList** gapPos =
            reinterpret_cast<NodeList**>(pos - gapSize);
        NodeList* gap = *gapPos;
        if (!gap) {
          break;
        }
        worklist.push(pos);
        worklist.push(end);
        pos = gap->buf + gapSize;
        end = gap->pos;
      }
    }

    iterator () {};

   public:
    static iterator theEnd() {
      iterator i;
      i.pos = i.end = -1;
      return i;
    }

    iterator(NodeList* cur) : cur(cur),
                              pos(cur->buf + gapSize),
                              end(cur->pos) {
        findStart();
    }

    bool operator != (iterator& other) {
      assert((int)other.pos == -1 && (int)other.end == -1);
      return !isEnd();
    }

    bool isEnd() {
      while (pos == end && !worklist.empty()) {
        popWorklist();
      }
      return worklist.empty() && pos == end;
    }

    void operator ++ () {
      if (cur->next && pos == end) {
        cur = cur->next;
        pos = cur->buf + gapSize;
        end = cur->pos;
      }
      if (pos == end) {
        while (pos == end) {
          assert(!worklist.empty());
          popWorklist();
        }
        return;
      }
      Node* n = this->operator*();
      pos += n->realSize() + gapSize;
      if (pos == end) {
        while (pos == end && !worklist.empty()) {
          popWorklist();
        }
      } else {
        findStart();
      }
    }

    Node* operator * () {
      return reinterpret_cast<Node*>(pos);
    }

   private:
    inline NodeList* insertBefore(NodeList* p) {
      NodeList** gap = reinterpret_cast<NodeList**>(pos - gapSize);
      if (!*gap) {
        *gap = new NodeList();
        p->gapsCache.add(*gap);
      }
      return *gap;
    }

    friend class NodeList;
  };

  friend class iterator;

  NodeList* insertBefore(iterator i) {
    return i.insertBefore(this);
  }

  iterator begin() {
    return iterator(this);
  }

  iterator end() {
    return iterator::theEnd();
  }

  iterator at(size_t pos) {
    iterator i = begin();
    for (size_t p = 0; p < pos; ++p) {
      ++i;
    }
    return i;
  }
};
