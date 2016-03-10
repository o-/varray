#include "ir-ll.h"

template <typename container>
void test(bool print = false) {
  auto bb = new NodeList<container>();
  auto* c1 = bb->push_back(new Constant(1));
  auto* c2 = bb->push_back(new Constant(2));
  auto* a = bb->push_back(new Add(c1, c2));

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  Node* c3 = new Constant(3);
  bb->insert(bb->at(1), c3);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto c4 = new Constant(4);
  bb->insert(bb->at(1), c4);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto a1 = new Add(c3, c4);
  bb->insert(bb->at(3), a1);
  bb->push_back(new Add(a1, a));

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto patch4 = bb->at(6);
  for (int i = 0; i < 40000; ++i) {
    auto c1 = new Constant(i);
    auto c2 = new Constant(i);
    patch4 = bb->insert(patch4, c2);
    patch4 = bb->insert(patch4, c1);
    ++patch4;
    ++patch4;
    patch4 = bb->insert(patch4, new Add(c1, c2));
    ++patch4;
  }

  int count = 0;
  // Simulate an analysis:
  for (auto i : *bb) {
    if (i->type == Node::Type::Add) {
      Node * l = ((Add*)i)->l();
      if (l->type == Node::Type::Constant) {
        count += ((Constant*)l)->value;
      }
    }
  }
  if (print)
    std::cout << count << "\n";
  assert(count == 799980004);

  delete bb;
}

int main(int argc, char *argv[]) {
  // test<nodeDeque>(true);

  assert(argc == 3);

  int count = atoi(argv[2]);

  if (strcmp("list", argv[1]) == 0) {
    clock_t begin = clock();

    for (int i = 0; i < count; ++i) {
      test<nodeList>();
    }

    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;

    std::cerr << "Linked list (" << count << ") took : "
              << elapsed_secs << "\n";
  } else {
    clock_t begin = clock();

    for (int i = 0; i < count; ++i) {
      test<nodeDeque>();
    }

    clock_t end = clock();
    double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;

    std::cerr << "Deque       (" << count << ") took : "
              << elapsed_secs << "\n";
  }

  return 0;
}
