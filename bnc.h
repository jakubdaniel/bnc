#ifndef __BNC_H__
#define __BNC_H__

#define WORDS (1 << (sizeof(Value) * 8))

typedef unsigned char Value;
typedef unsigned char Byte;
typedef size_t        Count;

typedef enum
{
  ZERO = 0,
  ONE  = 1
} Bit;

typedef struct Node Node;
typedef struct NodeClass NodeClass;

typedef struct NodeVisitor NodeVisitor;
typedef struct NodeVisitorClass NodeVisitorClass;

struct NodeClass
{
  void (*accept)  (Node* node, NodeVisitor* visitor);
  void (*destroy) (Node* node);
};

struct Node
{
  NodeClass* class;

  Count count;
  Count bit_count; 
};

void  node_accept        (Node* node, NodeVisitor* visitor);
Count node_get_count     (Node* node);
Count node_get_bit_count (Node* node);
void  node_delete        (Node* node);

typedef struct InnerNode InnerNode;
typedef struct LeafNode  LeafNode;

struct InnerNode
{
  Node parent;

  Node* left;
  Node* right;
};

InnerNode* inner_node_new (Node* left, Node* right);

struct LeafNode
{
  Node parent;

  Value value;
};

LeafNode* leaf_node_new (const Value value, const Count count);

struct NodeVisitorClass
{
  void (*visit_inner_node) (NodeVisitor* visitor, InnerNode* node);
  void (*visit_leaf_node)  (NodeVisitor* visitor, LeafNode*  node);
  void (*destroy)          (NodeVisitor* visitor);
};

struct NodeVisitor
{
  NodeVisitorClass* class;
};

void node_visitor_visit_inner_node (NodeVisitor* visitor, InnerNode* node);
void node_visitor_visit_leaf_node  (NodeVisitor* visitor, LeafNode*  node);
void node_visitor_delete           (NodeVisitor* visitor);

typedef struct BitVector BitVector;

struct BitVector
{
  Count count;
  Count size;
  Byte* bytes;
};

BitVector* bit_vector_new    (void);
BitVector* bit_vector_copy   (BitVector* vector);
void       bit_vector_push   (BitVector* vector, const Bit bit);
void       bit_vector_pop    (BitVector* vector);
void       bit_vector_delete (BitVector* vector);

typedef struct Tree Tree;

struct Tree
{
  NodeVisitor parent;
  BitVector* tree;
  BitVector* path;
  BitVector* translations[WORDS];

  Count count;
  Count bit_count;
  Node* table[WORDS];
};

Tree* tree_new      (/* BitStream */);
void  tree_register (Tree* tree, const Value value);
void  tree_load     (Tree* tree);
void  tree_build    (Tree* tree);
void  tree_write    (Tree* tree, const Value value);
void  tree_read     (Tree* tree, Value* value);
void  tree_delete   (Tree* tree);

#endif /* __BNC_H__ */
