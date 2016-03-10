#include <cstdint>
#include <cassert>
#include <cstring>
#include <string>
#include <stack>
#include <iostream>
#include <array>
#include <functional>

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

  // This is used to iterate the cells of the varray
  class flatIterator {
   public:
    uintptr_t finger_;

    flatIterator(uintptr_t start, uintptr_t end) : finger_(start) {}

    static flatIterator begin(NodeList* list) {
      return flatIterator(list->buf + gapSize, list->pos);
    }

    static flatIterator end(NodeList* list) {
      return flatIterator(list->pos, list->pos);
    }

    static flatIterator invalid() {
      return flatIterator(-1, -1);
    }

    bool operator == (const flatIterator & other) {
      return finger_ == other.finger_;
    }

    inline bool operator != (const flatIterator & other) const {
      return finger_ != other.finger_;
    }

    inline void operator ++ () {
      auto node = get();
      finger_ += gapSize + node->realSize();
    }

    void setGap(NodeList* gap) {
      *reinterpret_cast<NodeList**>(finger_ - gapSize) = gap;
    }

    inline bool hasGap() {
      return *reinterpret_cast<NodeList**>(finger_ - gapSize) != nullptr;
    }

    inline NodeList* gap() {
      return *reinterpret_cast<NodeList**>(finger_ - gapSize);
    }

    inline Node* get() {
      return reinterpret_cast<Node*>(finger_);
    }
  };

  class nextIterator {
   public:
    flatIterator it_;
    flatIterator end_;
    NodeList* cur_;

    nextIterator(flatIterator start, flatIterator end, NodeList* list) :
      it_(start), end_(end), cur_(list) {}

    static nextIterator begin(NodeList* list) {
      return nextIterator(
          flatIterator::begin(list), flatIterator::end(list), list);
    }

    static nextIterator end(NodeList* list) {
      return nextIterator(
          flatIterator::invalid(), flatIterator::invalid(), nullptr);
    }

    inline void operator ++ () {
      ++it_;
      if (it_ != end_) return;
      cur_ = cur_->next;
      if (cur_) {
        it_ = flatIterator::begin(cur_);
        end_ = flatIterator::end(cur_);
      }
    }

    inline bool operator != (const nextIterator& other) const {
      // Other is the end marker -> we are equal to the end marker
      // if we are at end.
      if (!other.cur_)
        return it_ != end_;

      if (!cur_)
        return other.it_ != other.end_;

      return it_ != other.it_;
    }

    void setGap(NodeList* gap) {
      it_.setGap(gap);
    }

    bool hasGap() {
      return it_.hasGap();
    }

    NodeList* gap() {
      return it_.gap();
    }

    Node* get() {
      return it_.get();
    }

  };

  // This class is used to access the gaps
  class Gaps {
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

    class gapIterator {
      flatIterator it_;
      flatIterator end_;

      void findNextGap() {
        while (it_ != end_ && !it_.hasGap())
          ++it_;
      }

      gapIterator(flatIterator begin, flatIterator end) : it_(begin),
                                                          end_(end) {
        findNextGap();
      }

     public:
      static gapIterator begin(NodeList* list) {
        return gapIterator(flatIterator::begin(list), flatIterator::end(list));
      }

      static gapIterator end(NodeList* list) {
        return gapIterator(flatIterator::end(list), flatIterator::end(list));
      }

      inline bool operator != (const gapIterator & other) const {
        return it_ != other.it_;
      }

      inline void operator ++ () {
        ++it_;
        findNextGap();
      }

      inline NodeList* operator * () {
        return it_.gap();
      }
    };

   public:
    void add(NodeList* l) {
      gapsCache.add(l);
    }

    void foreach(NodeList* parent, std::function<void(NodeList*)> f) {
      if (gapsCache.overflow()) {
        for (auto i = gapIterator::begin(parent);
             i != gapIterator::end(parent);
             ++i) {
          f(*i);
        }
      } else {
        for (auto g : gapsCache) {
          f(g);
        }
      }
    }
  };
  Gaps gaps;

  NodeList* next = nullptr;

 public:
  size_t totalSize() {
    size_t sum = size;
    gaps.foreach(this, [&sum](NodeList* gap) {
      sum += gap->totalSize();
    });
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
    gaps.foreach(this, [](NodeList* gap) {
      delete gap;
    });
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
    NodeList* cur = i.cur();
    uintptr_t bulkFixupStart = i.curFinger();
    uintptr_t bulkFixupEnd = i.curFinger();

    for (; i != end(); ++i) {
      if (cur != i.cur()) {
        flat->pos = fixup(bulkFixupStart, bulkFixupEnd, flat->pos);
        cur = i.cur();
        bulkFixupStart = i.curFinger();
      }
      bulkFixupEnd = i.curFinger();
    }
    flat->pos = fixup(bulkFixupStart, bulkFixupEnd, flat->pos);
    delete this;

    return flat;
  }

  class iterator {
    nextIterator it_;
    nextIterator end_;
    std::stack<nextIterator> worklist_;

    inline void findStart() {
      while (it_ != end_ && it_.hasGap()) {
        worklist_.push(it_);
        worklist_.push(end_);
        it_ = nextIterator::begin(it_.gap());
        end_ = nextIterator::end(it_.gap());
      }
    }

    inline void findNext() {
      while (!(it_ != end_) && !worklist_.empty()) {
        end_ = worklist_.top();
        worklist_.pop();
        it_ = worklist_.top();
        worklist_.pop();
      }
    }

    iterator(nextIterator start, nextIterator end) : it_(start), end_(end) {
      findStart();
    }

   public:
    static iterator begin(NodeList* cur) {
      return iterator(nextIterator::begin(cur), nextIterator::end(cur));
    }

    static iterator end(NodeList* cur) {
      return iterator(nextIterator::end(cur), nextIterator::end(cur));
    }

    inline void operator ++ () {
      ++it_;
      findStart();
      findNext();
    }

    inline bool operator != (const iterator& other) const {
      return it_ != other.it_;
    }

    inline Node* operator * () {
      return it_.get();
    }

    NodeList* cur() {
      return it_.cur_;
    }

    uintptr_t curFinger() {
      return it_.it_.finger_;
    }

    uintptr_t curEnd() {
      return it_.end_.finger_;
    }

   private:
    inline NodeList* insertBefore(NodeList* p) {
      if (!it_.hasGap()) {
        it_.setGap(new NodeList);
        p->gaps.add(it_.gap());
      }
      return it_.gap();
    }

    friend class NodeList;
  };

  NodeList* insertBefore(iterator i) {
    return i.insertBefore(this);
  }

  iterator begin() {
    return iterator::begin(this);
  }

  iterator end() {
    return iterator::end(this);
  }

  iterator at(size_t pos) {
    iterator i = begin();
    for (size_t p = 0; p < pos; ++p) {
      ++i;
    }
    return i;
  }
};
