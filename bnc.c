#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include <bnc.h>

#define MAP_SIZE ((Count)4096 * sysconf(_SC_PAGESIZE))

#define CUT_LOWER(n, m)     ((n) &   ((1 << (m)) - 1))
#define CUT_OFF_LOWER(n, m) ((n) & (~((1 << (m)) - 1)))
#define CUT_UPPER(n, m)     ((n) & (~((1 << (8 - (m))) - 1)))
#define CUT_OFF_UPPER(n, m) ((n) &   ((1 << (8 - (m))) - 1))

/**
 * Little endian - Big endian
 */
#define htonll(n) (htonl((Count)(n) >> 32) | ((Count)htonl(n) << 32))
#define ntohll(n) (ntohl((Count)(n) >> 32) | ((Count)ntohl(n) << 32))

#define STREAM_DEBUG false

#if STREAM_DEBUG
static void print_binary (Byte* bytes, Count offset, Count count)
{
  Count i;

  for (i = offset; i < offset + count; ++i)
  {
    printf("%i", (bytes[i / 8] & (1 << (i % 8))) ? 1 : 0);
    if ((i + 1) % 8 == 0)
    {
      printf(" ");
    }
  }

  printf("\n");
}
#endif

static char* pretty_print_size (Count size)
{
  const char* units[] =
  {
    "B", "KiB", "MiB", "GiB", "TiB", "PiB"
  };

  Count copy  = size;
  Count power = 0;
  Count necessary;
  float float_size;
  char* buffer;
  const char* format = "%.4f %s";

  while ((size >> 10) && power < sizeof(units) / sizeof(*units))
  {
    size >>= 10;
    ++power;
  }

  float_size = ((float)copy) / (1 << (10 * power));

  necessary = snprintf(NULL, 0, format, float_size, units[power]) + 1;

  buffer = (char*)malloc(necessary * sizeof(char));
  
  snprintf(buffer, necessary, format, float_size, units[power]);

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
  bit_vector->bytes = (Byte*)malloc((bit_vector->size + 2) * sizeof(Byte));

  return bit_vector;
}

BitVector* bit_vector_copy (BitVector* vector)
{
  BitVector* bit_vector = (BitVector*)malloc(sizeof(BitVector));

  bit_vector->count = vector->count;
  bit_vector->size  = vector->size;
  bit_vector->bytes = (Byte*)malloc(bit_vector->size * sizeof(Byte));

  memcpy(bit_vector->bytes + 1, vector->bytes + 1, (bit_vector->count + 7) / 8);

  return bit_vector;
}

void bit_vector_push (BitVector* vector, const Bit bit)
{
  vector->bytes[vector->count / 8 + 1] &= ~(1 << (vector->count % 8));
  vector->bytes[vector->count / 8 + 1] |= bit << (vector->count % 8);

  ++vector->count;

  if ((vector->count + 7) / 8 + 1 > vector->size)
  {
    vector->bytes = (Byte*)realloc(vector->bytes, ((vector->size *= 2) + 2) * sizeof(Byte));
  }
}

void bit_vector_pop (BitVector* vector)
{
  --vector->count;

  if ((vector->count + 7) / 8 + 1 < vector->size / 2)
  {
    vector->bytes = (Byte*)realloc(vector->bytes, ((vector->size /= 2) + 2) * sizeof(Byte));
  }
}

void bit_vector_set_context (BitVector* vector, Byte left, Byte right)
{
  vector->bytes[0] = left;
  vector->bytes[(vector->count + 7) / 8 + 1 - 1] |= right << (vector->count % 8);
  vector->bytes[(vector->count + 7) / 8 + 1    ]  = right >> (8 - (vector->count % 8));
}

void bit_vector_delete (BitVector* vector)
{
  free(vector->bytes);
  free(vector);
}

static void bit_stream_load_block (BitStream* stream)
{
  stream->memory_block = mmap(NULL, MAP_SIZE, stream->protocol, MAP_SHARED, stream->backend, MAP_SIZE * (stream->offset / MAP_SIZE));
  stream->count        = (stream->count % 8) + (stream->offset % MAP_SIZE) * 8;
}

static void bit_stream_flush_block (BitStream* stream)
{
  munmap(stream->memory_block, MAP_SIZE);
  stream->offset = MAP_SIZE * (stream->offset / MAP_SIZE + 1);
}

BitStream* bit_stream_new (int backend, int protocol, Count offset)
{
  BitStream* stream = (BitStream*)malloc(sizeof(BitStream));

  stream->count    = 0;
  stream->offset   = offset;
  stream->backend  = backend;
  stream->protocol = protocol;

  bit_stream_load_block(stream);

  return stream;
}

static void bit_stream_write_byte (BitStream* stream, BitVector* vector, Count shift, Count stream_offset, Count vector_offset)
{
  if ((stream_offset + 7) / 8 >= MAP_SIZE)
  {
    bit_stream_flush_block(stream);
    bit_stream_load_block(stream);
    bit_stream_write_byte(stream, vector, shift, 0, vector_offset);
  }
  else
  {
    Byte* destination = stream->memory_block;
    Byte* source      = vector->bytes;

    destination[stream_offset / 8] = CUT_LOWER(source[vector_offset / 8] >> (8 - shift), shift) | CUT_UPPER(source[vector_offset / 8 + 1] << shift, 8 - shift);
  }
}

void bit_stream_write (BitStream* stream, BitVector* vector)
{
  Count i;
  Count shift = stream->count % 8;
  Byte left  = 0;
  Byte right = 0;

  if (stream->count > 0)
  {
    left = stream->memory_block[(stream->count + 7) / 8 - 1] << (8 - shift);
  }

  bit_vector_set_context(vector, left, right);

#if STREAM_DEBUG
  printf("B:\n");
  print_binary(stream->memory_block, 0, stream->count);
#endif

  for (i = 0; i < vector->count + 7; i += 8)
  {
    bit_stream_write_byte(stream, vector, shift, stream->count + i, i);
  }

  stream->count += vector->count;

#if STREAM_DEBUG
  printf("A:\n");
  print_binary(vector->bytes + 1, 0, vector->count);
  print_binary(stream->memory_block, 0, stream->count);
#endif
}

void bit_stream_read (BitStream* stream, Bit* bit)
{
  if ((stream->count + 7) / 8 >= MAP_SIZE)
  {
    bit_stream_flush_block(stream);
    bit_stream_load_block(stream);
  }

  *bit = (stream->memory_block[stream->count / 8] & (1 << (stream->count % 8))) ? ONE : ZERO;

  ++stream->count;
}

void bit_stream_delete (BitStream* stream)
{
  bit_stream_flush_block(stream);
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

  /**
   * To avoid overwrites and memory leaks when there are dummy leaf nodes with the same value
   */
  if (tree->translations[node->value] == NULL)
  {
    tree->translations[node->value] = bit_vector_copy(tree->path);
  }
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

  for (i = 0; i < tree->count; ++i)
  {
    node_delete(tree->table[i]);
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
  tree->count     = 0;
  
  return tree;
}

void tree_empty (Tree* tree)
{
  Count i;

  tree->count = WORDS;

  for (i = 0; i < tree->count; ++i)
  {
    tree->table[i] = (Node*)leaf_node_new(i, 0);
  }
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
    ++tree->count;
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

static Node* tree_load (Tree* tree)
{
  Bit bit;
  Count i;
  Value value = 0;
  Node* left;
  Node* right;

  bit_stream_read(tree->stream, &bit);

  switch (bit)
  {
    case ZERO:
      left  = tree_load(tree);
      right = tree_load(tree);

      return (Node*)inner_node_new(left, right);
    case ONE:
      for (i = 0; i < sizeof(Value) * 8; ++i)
      {
        bit_stream_read(tree->stream, &bit);

	value |= bit << i;
      }

      return (Node*)leaf_node_new(value, 0);
  }

  return NULL;
}

void tree_set_write_stream (Tree* tree, BitStream* stream)
{
  tree->stream = stream;

  bit_stream_write(tree->stream, tree->tree);
}

void tree_set_read_stream (Tree* tree, BitStream* stream)
{
  tree->stream = stream;

  tree->table[0] = tree_load(tree);
  tree->count    = 1;
}

void tree_write (Tree* tree, const Value value)
{
  BitVector* translation = tree->translations[value];

  bit_stream_write(tree->stream, translation);
}

void tree_read (Tree* tree, Value* value)
{
  Node* cursor = tree->table[0];

  while (cursor->class == &inner_node_class)
  {
    Bit bit;

    bit_stream_read(tree->stream, &bit);

    switch (bit)
    {
      case ZERO: cursor = ((InnerNode*)cursor)->left;  break;
      case ONE:  cursor = ((InnerNode*)cursor)->right; break;
    }
  }

  *value = ((LeafNode*)cursor)->value;
}

void tree_delete (Tree* tree)
{
  tree->parent.class->destroy((NodeVisitor*)tree);
}

File* file_new (const char* name)
{
  File* file = (File*)malloc(sizeof(File));

  file->name = (char*)malloc((strlen(name) + 1) * sizeof(char));
  strcpy(file->name, name);

  file->size = 0;
  file->compressed_size = 0;
  file->offset = 0;

  return file;
}

void file_open_read (File* file)
{
  int c;

  file->backend = fopen(file->name, "rb");
  file->tree    = tree_new();

  tree_empty(file->tree);

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
  file->backend = fopen(file->name, "wb+");
  file->tree    = tree_new();
}

void file_read (File* file, int backend)
{
  Count count = file->size;

  BitStream* stream = bit_stream_new(backend, PROT_READ, file->offset);

  tree_set_read_stream(file->tree, stream);

  while (count > 0)
  {
    Value value;

    tree_read(file->tree, &value);

    fputc((int)value, file->backend);

    --count;
  }
}

void file_write (File* file, int backend)
{
  int c;
  BitStream* stream = bit_stream_new(backend, PROT_READ|PROT_WRITE, file->offset);

  tree_set_write_stream(file->tree, stream);

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

  archive->name = (char*)malloc((strlen(name) + 1) * sizeof(char));
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
  Count offset = 0;
  Count count       = htonll(archive->files_count);
  Count head_length = 0;
  int backend       = open(archive->name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

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

  for (i = 0; i < archive->files_count; ++i)
  {
    archive->files[i]->offset = offset;
    offset += archive->files[i]->compressed_size;
  }

  /**
   * Stretch the file
   */
  lseek(backend, offset, SEEK_SET);
  write(backend, "", 1);

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_write(archive->files[i], backend);
  }

  /**
   * Write head
   */
  write(backend, &count, sizeof(Count));

  for (i = 0; i < archive->files_count; ++i)
  {
    char* name;
    Count name_length;
    Count size            = htonll(archive->files[i]->size);
    Count compressed_size = htonll(archive->files[i]->compressed_size);

    name        = strrchr(archive->files[i]->name, '/');
    name        = name ? name + 1 : archive->files[i]->name;
    name_length = strlen(name);

    head_length += sizeof(Count) + name_length + 2 * sizeof(Count);

    name_length = htonll(name_length);

    write(backend, &name_length,     sizeof(Count));
    write(backend, name,             strlen(name));
    write(backend, &size,            sizeof(Count));
    write(backend, &compressed_size, sizeof(Count));

  }
  head_length += 2 * sizeof(Count);
  head_length = htonll(head_length);

  write(backend, &head_length, sizeof(Count));

  close(backend);
}

void archive_decompress (Archive* archive)
{
  Count i;
  Count offset = 0;
  Count count;
  Count head_length;
  int backend = open(archive->name, O_RDONLY);

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_open_write(archive->files[i]);
  }

  /**
   * Read head
   */
  lseek(backend, -sizeof(Count), SEEK_END);
  read(backend, &head_length, sizeof(Count));
  head_length = ntohll(head_length);

  lseek(backend, -head_length, SEEK_END);
  read(backend, &count, sizeof(Count));
  count = ntohll(count);

  for (i = 0; i < count; ++i)
  {
    Count j;
    Count name_length;
    Count size;
    Count compressed_size;
    char* file_size;
    char* file_compressed_size;
    char* name;

    read(backend, &name_length, sizeof(Count));
    name_length = ntohll(name_length);

    name = (char*)malloc((name_length + 1) * sizeof(char));
    name[name_length] = '\0';

    read(backend, name, name_length);

    read(backend, &size,            sizeof(Count));
    read(backend, &compressed_size, sizeof(Count));

    size            = ntohll(size);
    compressed_size = ntohll(compressed_size);

    file_size            = pretty_print_size(size);
    file_compressed_size = pretty_print_size(compressed_size);

    printf("File `%s` %s >> %s\n", name, file_size, file_compressed_size);

    free(file_size);
    free(file_compressed_size);

    for (j = 0; j < archive->files_count; ++j)
    {
      if (strcmp(name, archive->files[j]->name) == 0)
      {
        archive->files[j]->size            = size;
	archive->files[j]->compressed_size = compressed_size;
        archive->files[j]->offset          = offset;
      }
    }

    free(name);

    offset += compressed_size;
  }

  #pragma omp parallel for
  for (i = 0; i < archive->files_count; ++i)
  {
    file_read(archive->files[i], backend);
  }

  close(backend);
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
  int i;

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
