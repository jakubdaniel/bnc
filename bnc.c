#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/mman.h>

#include <bnc.h>

static char* pretty_print_size (Count size)
{
  const char* units[] =
  {
    "B", "KiB", "MiB", "GiB", "TiB", "PiB"
  };

  Count power = 0;
  Count necessary;
  float float_size;
  char* buffer;

  while (size >> 10 && power < sizeof(units) / sizeof(*units))
  {
    size >>= 10;
    ++power;
  }

  float_size = ((float)size) / (1 << (10 * (power - 1)));

  necessary = snprintf(NULL, 0, "%.2f %s", float_size, units[power]);

  buffer = (char*)malloc((necessary + 1) * sizeof(char));
  
  snprintf(buffer, necessary, "%.2f %s", float_size, units[power]);

  return buffer;
}

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

BitStream* bit_stream_new (int backend, Count size, int protocol, Count offset)
{
  BitStream* stream = (BitStream*)malloc(sizeof(BitStream*));

  stream->memory_block = mmap(NULL, size, protocol, MAP_SHARED, backend, offset);
  stream->size = size;
  stream->count = 0;

  return stream;
}

void bit_stream_write (BitStream* stream, BitVector* vector)
{
  /**
   * Transfer quickly
   */
}

void bit_stream_read (BitStream* stream, Bit* bit)
{
}

void bit_stream_delete (BitStream* stream)
{
  munmap(stream->memory_block, stream->size);
  free(stream);
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
  Count i;

  /**
   * Serialise leaf
   */
  bit_vector_push(tree->tree, ONE);

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
  Count i;

  bit_stream_delete(tree->stream);

  bit_vector_delete(tree->tree);
  bit_vector_delete(tree->path);

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
  Count i;

  tree->parent.class = &tree_class;

  tree->tree = bit_vector_new();
  tree->path = bit_vector_new();

  memset(tree->translations, 0, sizeof(tree->translations));

  tree->bit_count = 0;
  tree->count     = WORDS;

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
}

void tree_set_stream (Tree* tree, BitStream* stream)
{
  tree->stream = stream;
}

void tree_write (Tree* tree, const Value value)
{
  BitVector* translation = tree->translations[value];

  bit_stream_write(tree->stream, translation);
}

void tree_read (Tree* tree, Value* value)
{
}

void tree_delete (Tree* tree)
{
  tree->parent.class->destroy((NodeVisitor*)tree);
}

File* file_new (const char* name)
{
  File* file = (File*)malloc(sizeof(File));

  file->name = (char*)malloc(strlen(name) + 1);
  strcpy(file->name, name);

  return file;
}

void file_open_read (File* file)
{
  int c;

  file->backend = fopen(file->name, "rb");
  file->tree    = tree_new();

  fseek(file->backend, 0, SEEK_SET);

  c = fgetc(file->backend);

  while (c != EOF)
  {
    tree_register(file->tree, c);

    c = fgetc(file->backend);
  }

  tree_build(file->tree);

  file->size            = ftell(file->backend);
  file->compressed_size = (file->tree->bit_count + file->tree->table[0]->bit_count + 7) / 8;

  fseek(file->backend, 0, SEEK_SET);
}

void file_open_write (File* file)
{
}

void file_read (File* file, BitStream* stream)
{
}

void file_write (File* file, int backend, Count offset)
{
  int c;
  BitStream* stream = bit_stream_new(backend, file->compressed_size, PROT_WRITE, offset);

  tree_set_stream(file->tree, stream);

  c = fgetc(file->backend);

  while (c != EOF)
  {
    tree_write(file->tree, c);

    c = fgetc(file->backend);
  }
}

void file_delete (File* file)
{
  tree_delete(file->tree);
  fclose(file->backend);
  free(file->name);
  free(file);
}

Archive* archive_new (const char* name)
{
  Archive* archive = (Archive*)malloc(sizeof(Archive));

  archive->name = (char*)malloc(strlen(name) + 1);
  strcpy(archive->name, name);

  archive->files = NULL;
  archive->files_count = 0;

  return archive;
}

void archive_add_file (Archive* archive, const char* file)
{
  archive->files = (File**)realloc(archive->files, (++archive->files_count) * sizeof(File*));
  archive->files[archive->files_count - 1] = file_new(file);
}

void archive_compress (Archive* archive)
{
  Count i;
  Count* offset = (Count*)malloc(archive->files_count * sizeof(Count));
  int backend = open(archive->name, O_WRONLY);

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    char* file_size;
    char* file_compressed_size;

    file_open_read(archive->files[i]);

    file_size            = pretty_print_size(archive->files[i]->size);
    file_compressed_size = pretty_print_size(archive->files[i]->compressed_size);

    printf("File `%s` %s >> %s\n", archive->files[i]->name, file_size, file_compressed_size);

    free(file_size);
    free(file_compressed_size);
  }

  offset[0] = 0;

  for (i = 1; i < archive->files_count; ++i)
  {
    offset[i] = offset[i - 1] + archive->files[i]->compressed_size;
  }

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_write(archive->files[i], backend, offset[i]);
  }

  /**
   * TODO: write head
   */
}

void archive_decompress (Archive* archive)
{
  Count i;

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_open_write(archive->files[i]);
  }

  /**
   * TODO: read head
   */

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_read(archive->files[i], NULL);
  }
}

void archive_delete (Archive* archive)
{
  Count i;

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_delete(archive->files[i]);
  }

  free(archive->files);
  free(archive->name);
  free(archive);
}

const char* help = "./bnc [bu] archive file1 file2 ...";

int main (int argc, char** argv)
{
  char op;
  Archive* archive;
  Count i;

  if (argc < 3)
  {
    printf("%s\n", help);

    return EXIT_FAILURE;
  }

  op = argv[1][0];

  archive = archive_new(argv[2]);

  argc -= 3;
  argv += 3;

  for (i = 0; i < argc; ++i)
  {
    archive_add_file(archive, argv[i]);
  }

  switch (op)
  {
    case 'b': archive_compress(archive); break;
    case 'u': archive_decompress(archive); break;
  }

  archive_delete(archive);

  return EXIT_SUCCESS;
}
