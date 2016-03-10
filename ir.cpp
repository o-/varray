#include "ir.h"


void test(bool print = false) {
  auto bb = new NodeList();
  auto* c1 = bb->insert<Constant>(1);
  auto* c2 = bb->insert<Constant>(2);
  auto* a = bb->insert<Add>(c1, c2);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto patch = bb->insertBefore(bb->at(1));
  auto c3 = patch->insert<Constant>(3);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto patch2 = bb->insertBefore(bb->at(1));
  auto c4 = patch2->insert<Constant>(4);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto patch3 = bb->insertBefore(bb->at(3));
  auto a1 = patch3->insert<Add>(c3, c4);
  bb->insert<Add>(a1, a);

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  bb = bb->flatten();

  if (print) {
    for (auto i : *bb)
      std::cout << *i << "\n";
    std::cout << "===============\n";
  }

  auto patch4 = bb->insertBefore(bb->at(6));
  for (int i = 0; i < 40000; ++i) {
    patch4->insert<Add>(
        patch4->insert<Constant>(i),
        patch4->insert<Constant>(i));
  }

  bb = bb->flatten();

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

  delete bb;
}

using namespace std;

int main(int argc, char *argv[]) {
  test(true);

  assert(argc == 2);

  int count = atoi(argv[1]);

  clock_t begin = clock();

  for (int i = 0; i < count; ++i) {
    test();
  }

  clock_t end = clock();
  double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;

  std::cerr << "Varray      (" << count << ") took : "
            << elapsed_secs << "\n";

  return 0;
}
