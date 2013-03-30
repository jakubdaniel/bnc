#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bnc.h>

Count node_get_count (Node* node)
{
  return node->count;
}

Count node_get_bit_count (Node* node)
{
  return node->bit_count;
}

static void inner_node_accept (Node* node, NodeVisitor* visitor)
{
  node_visitor_visit_inner_node(visitor, (InnerNode*)node);
}

static void inner_node_destroy (Node* node)
{
  node_delete(((InnerNode*)node)->left);
  node_delete(((InnerNode*)node)->right);
  free(node);
}

static void leaf_node_destroy (Node* node)
{
  free(node);
}

static void leaf_node_accept (Node* node, NodeVisitor* visitor)
{
  node_visitor_visit_leaf_node(visitor, (LeafNode*)node);
}

static NodeClass inner_node_class =
{
  inner_node_accept,
  inner_node_destroy
};

static NodeClass leaf_node_class =
{
  leaf_node_accept,
  leaf_node_destroy
};

void node_accept (Node* node, NodeVisitor* visitor)
{
  node->class->accept(node, visitor);
}

void node_delete (Node* node)
{
  node->class->destroy(node);
}

InnerNode* inner_node_new (Node* left, Node* right)
{
  InnerNode* inner_node = (InnerNode*)malloc(sizeof(InnerNode));

  inner_node->parent.class     = &inner_node_class;
  inner_node->parent.count     = left->count + right->count;
  inner_node->parent.bit_count = (left->bit_count + left->count) + (right->bit_count + right->count);
  inner_node->left  = left;
  inner_node->right = right;

  return inner_node;
}

LeafNode* leaf_node_new (const Value value, const Count count)
{
  LeafNode* leaf_node = (LeafNode*)malloc(sizeof(LeafNode));

  leaf_node->parent.class     = &leaf_node_class;
  leaf_node->parent.count     = count;
  leaf_node->parent.bit_count = 0;
  leaf_node->value = value;

  return leaf_node;
}

void node_visitor_visit_inner_node (NodeVisitor* visitor, InnerNode* node)
{
  visitor->class->visit_inner_node(visitor, node);
}

void node_visitor_visit_leaf_node (NodeVisitor* visitor, LeafNode* node)
{
  visitor->class->visit_leaf_node(visitor, node);
}

void node_visitor_delete (NodeVisitor* visitor)
{
  visitor->class->destroy(visitor);
}

ABCD_dump (BitVector* vector)
{
  Count i;
  for (i = 0; i < vector->count; ++i)
  {
    printf("%i", (vector->bytes[i / 8] & (1 << (i % 8))) ? 1 : 0);
  }
  printf("\n");
}

BitVector* bit_vector_new (void)
{
  BitVector* bit_vector = (BitVector*)malloc(sizeof(BitVector));

  bit_vector->count = 0;
  bit_vector->size  = 1;
  bit_vector->bytes = (Byte*)malloc(bit_vector->size);

  return bit_vector;
}

BitVector* bit_vector_copy (BitVector* vector)
{
  BitVector* bit_vector = (BitVector*)malloc(sizeof(BitVector));

  bit_vector->count = vector->count;
  bit_vector->size  = vector->size;
  bit_vector->bytes = (Byte*)malloc(bit_vector->size);

  memcpy(bit_vector->bytes, vector->bytes, (bit_vector->count + 7) / 8);

  return bit_vector;
}

void bit_vector_push (BitVector* vector, const Bit bit)
{
  vector->bytes[vector->count / 8] &= ~(1 << (vector->count % 8));
  vector->bytes[vector->count / 8] |= bit << (vector->count % 8);

  ++vector->count;

  if ((vector->count + 7) / 8 + 1 > vector->size)
  {
    vector->bytes = (Byte*)realloc(vector->bytes, vector->size *= 2);
  }
}

void bit_vector_pop (BitVector* vector)
{
  --vector->count;

  if ((vector->count + 7) / 8 + 1 < vector->size / 2)
  {
    vector->bytes = (Byte*)realloc(vector->bytes, vector->size /= 2);
  }
}

void bit_vector_delete (BitVector* vector)
{
  free(vector->bytes);
  free(vector);
}

static void tree_visit_inner_node (NodeVisitor* visitor, InnerNode* node)
{
  Tree* tree = (Tree*)visitor;

  /**
   * Add an inner node into the serialised tree
   */
  bit_vector_push(tree->tree, ZERO);

  /**
   * Construct paths for individual leaves
   */
  bit_vector_push(tree->path, ZERO);
  node_accept(node->left, visitor);
  bit_vector_pop(tree->path);

  bit_vector_push(tree->path, ONE);
  node_accept(node->right, visitor);
  bit_vector_pop(tree->path);
}

static void tree_visit_leaf_node (NodeVisitor* visitor, LeafNode* node)
{
  Tree* tree = (Tree*)visitor;

  /**
   * Serialise leaf
   */
  bit_vector_push(tree->tree, ONE);

  Count i;

  for (i = 0; i < sizeof(Value) * 8; ++i)
  {
    if (node->value & (1 << i))
    {
      bit_vector_push(tree->tree, ONE);
    }
    else
    {
      bit_vector_push(tree->tree, ZERO);
    }
  }

  tree->translations[node->value] = bit_vector_copy(tree->path);
}

static void tree_destroy (NodeVisitor* visitor)
{
  Tree* tree = (Tree*)visitor;

  bit_vector_delete(tree->tree);
  bit_vector_delete(tree->path);

  Count i;

  for (i = 0; i < WORDS; ++i)
  {
    BitVector* vector = tree->translations[i];

    if (vector) bit_vector_delete(vector);
  }

  free(tree);
}

static NodeVisitorClass tree_class =
{
  tree_visit_inner_node,
  tree_visit_leaf_node,
  tree_destroy
};

Tree* tree_new (void)
{
  Tree* tree = (Tree*)malloc(sizeof(Tree));

  tree->parent.class = &tree_class;

  tree->tree = bit_vector_new();
  tree->path = bit_vector_new();

  memset(tree->translations, 0, sizeof(tree->translations));

  tree->bit_count = 0;
  tree->count     = WORDS;

  Count i;
  
  for (i = 0; i < tree->count; ++i)
  {
    tree->table[i] = (Node*)leaf_node_new(i, 0);
  }

  return tree;
}

void tree_register (Tree* tree, const Value value)
{
  ++tree->table[value]->count;
}

static int tree_compare_nodes (const void* first, const void* second)
{
  const Node* first_node  = *(const Node**)first;
  const Node* second_node = *(const Node**)second;

  if (first_node->count < second_node->count) return  1;
  if (first_node->count > second_node->count) return -1;

  return 0;
}

static void tree_sort (Tree* tree)
{
  qsort(tree->table, tree->count, sizeof(Node*), tree_compare_nodes);
}

static void tree_clear (Tree* tree)
{
  tree_sort(tree);

  while (tree->count > 0 && tree->table[tree->count - 1]->count == 0)
  {
    node_delete(tree->table[tree->count - 1]);

    --tree->count;
  }
}

void tree_build (Tree* tree)
{
  tree_clear(tree);

  /**
   * Just in case of a too small tree
   */
  while (tree->count < 2)
  {
    tree->table[tree->count] = (Node*)leaf_node_new('\0', 0);
  }

  /**
   * Each leaf is encoded by 1 bit followed by the Value
   */
  tree->bit_count = tree->count * (sizeof(Value) * 8 + 1);

  while (tree->count > 1)
  {
    tree->table[tree->count - 2] = (Node*)inner_node_new(tree->table[tree->count - 2], tree->table[tree->count - 1]);

    /**
     * Two nodes are replaced with their parent, each inner node is represented by 1 bit
     */
    --tree->count;
    ++tree->bit_count;

    tree_sort(tree);
  }

  /**
   * Compute translation table and serialised tree
   */
  node_accept(tree->table[0], (NodeVisitor*)tree);

  printf("LEAVES: %lu, TREE BITS: %lu, TRANSLATED BITS: %lu\n", tree->table[0]->count, tree->bit_count, tree->table[0]->bit_count);

  ABCD_dump(tree->tree);
}

void tree_write (Tree* tree, const Value value)
{
  ABCD_dump(tree->translations[value]);
}

void tree_read (Tree* tree, Value* value)
{
}

void tree_delete (Tree* tree)
{
  tree->parent.class->destroy((NodeVisitor*)tree);
}

int main (int argc, char** argv)
{
  Tree* tree = tree_new();
  tree_register(tree, 'a');
  tree_register(tree, 'a');
  tree_register(tree, 'a');
  tree_register(tree, 'a');
  tree_register(tree, 'b');
  tree_register(tree, 'C');
  tree_register(tree, 'C');
  tree_register(tree, 'C');
  tree_register(tree, 'x');
  tree_build(tree);
  tree_write(tree, 'a');
  tree_write(tree, 'b');
  tree_write(tree, 'C');
  tree_write(tree, 'x');
  tree_delete(tree);

  return EXIT_SUCCESS;
}
