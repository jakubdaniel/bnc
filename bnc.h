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

typedef struct BitStream BitStream;

struct BitStream
{
  Byte* memory_block;
  Count size;
  Count count;
};

BitStream* bit_stream_new    (int backend, Count size, int protocol, Count offset);
void       bit_stream_write  (BitStream* stream, BitVector* vector);
void       bit_stream_delete (BitStream* stream);

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

  BitStream* stream;
};

Tree* tree_new              (void);
void  tree_empty            (Tree* tree);
void  tree_register         (Tree* tree, const Value value);
void  tree_build            (Tree* tree);
void  tree_set_write_stream (Tree* tree, BitStream* stream);
void  tree_set_read_stream  (Tree* tree, BitStream* stream);
void  tree_write            (Tree* tree, const Value value);
void  tree_read             (Tree* tree, Value* value);
void  tree_delete           (Tree* tree);

typedef struct File File;

struct File
{
  FILE* backend;
  char* name;
  Tree* tree;

  Count size;
  Count compressed_size;
  Count offset;
};

File* file_new        (const char* name);
void  file_open_read  (File* file);
void  file_open_write (File* file);
void  file_read       (File* file, int backend);
void  file_write      (File* file, int backend);
void  file_delete     (File* file);

typedef struct Archive Archive;

struct Archive
{
  char* name;

  File** files;
  Count  files_count;
};

Archive* archive_new        (const char* name);
void     archive_add_file   (Archive* archive, const char* file);
void     archive_compress   (Archive* archive);
void     archive_decompress (Archive* archive);
void     archive_delete     (Archive* archive);

#endif /* __BNC_H__ */
