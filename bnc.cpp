#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <deque>
#include <map>
#include <vector>
#include <string>

using namespace std;

namespace bnc {
  class BitStream {
    public:
      typedef char Byte;
      typedef char Bit;
      typedef vector<Bit> Vector;

    private:
      size_t count;
      size_t compressed_size;
      Byte buffer;
      ifstream* in;
      ofstream* out;
      
    public:
      BitStream(ofstream& out) : out(&out), count(0), compressed_size(0) {
      }

      BitStream(ifstream& in) : in(&in), count(0), compressed_size(0) {
      }

      void operator<< (Bit bit) {
        if (count == 8) {
	  flush();
	}

	buffer |= ((bit & 1) << (8 - count - 1));
	++count;
      }

      void operator>> (Bit& bit) {
        if (count == 0) {
	  in->read(&buffer, 1);

	  count = 8;
	}

	--count;
        bit = (buffer >> count) & 1;
      }

      void operator<< (Vector& vector) {
        for (Bit bit : vector) {
	  operator<<(bit);
	}
      }

      void flush() {
        out->write(&buffer, 1);

	buffer = 0;
	count  = 0;
	++compressed_size;
      }

      size_t size() {
        return compressed_size;
      }

      void close() {
        if (count) {
	  flush();
	}
      }
  };
  class Node;
  class Leaf;
  class InnerNode;
  class NodeVisitor {
    public:
      virtual void visit(Leaf& leaf) = 0;
      virtual void visit(InnerNode& inner) = 0;
  };
  class Node {
    public:
      static Node* load(BitStream& bitstream);

      virtual void accept(NodeVisitor& visitor) = 0;
      virtual char run(BitStream& bitstream) = 0;
  };
  class Leaf : public Node {
    public:
      char value;

      Leaf(char value) : value(value) {
      }

      Leaf(BitStream& bitstream) : value(0) {
        for (int i = 0; i < 8; ++i) {
	  BitStream::Bit bit;

	  bitstream >> bit;

	  value |= bit;
	  value <<= 1;
	}
      }

      virtual void accept(NodeVisitor& visitor) {
        visitor.visit(*this);
      }

      virtual char run(BitStream& bitstream) {
        return value;
      }
  };
  class InnerNode : public Node {
    public:
      Node* left;
      Node* right;

      InnerNode(Node* left, Node* right) : left(left), right(right) {
      }

      InnerNode(BitStream& bitstream) {
        left  = load(bitstream);
	right = load(bitstream);
      }

      ~InnerNode() {
        delete left;
	delete right;
      }

      virtual void accept(NodeVisitor& visitor) {
        visitor.visit(*this);
      }

      virtual char run(BitStream& bitstream) {
        BitStream::Bit bit;

	bitstream >> bit;

	if (bit) {
	  return left->run(bitstream);
	} else {
	  return right->run(bitstream);
	}
      }
  };
  class TreeSerialiser : public NodeVisitor {
    private:
      BitStream& bitstream;
      BitStream::Vector path;

    public:
      map<char, BitStream::Vector> translations;

      TreeSerialiser(BitStream& bitstream) : bitstream(bitstream) {
      }

      virtual void visit(Leaf& leaf) {
        bitstream << 1;

	translations.insert(pair<char, BitStream::Vector>(leaf.value, path));

	for (int i = 7; i > 0; --i) {
	  bitstream << ((leaf.value >> i) & 1);
	}

	path.pop_back();
      }
      virtual void visit(InnerNode& inner) {
        bitstream << 0;

	path.push_back(1);
        inner.left->accept(*this);
	path.push_back(0);
	inner.right->accept(*this);

	path.pop_back();
      }
  };
  Node* Node::load(BitStream& bitstream) {
    BitStream::Bit bit;

    bitstream >> bit;

    if (bit) {
      return new Leaf(bitstream);
    } else {
      return new InnerNode(bitstream);
    }
  }
  class TreeStream {
    private:
      map<char, size_t> counts;
      map<char, BitStream::Vector> translations;
      BitStream& bitstream;
      Node* root;

    public:
      TreeStream(BitStream& bitstream) : bitstream(bitstream), root(NULL) {
      }
      ~TreeStream() {
        if (root) {
	  delete root;
	}
      }

      void operator<< (char byte) {
        if (!root) {
          if (counts.find(byte) != counts.end()) {
            ++counts[byte];
          } else {
	    counts[byte] = 1;
	  }
        } else {
	  bitstream << translations[byte];
	}
      }

      void operator>> (char& byte) {
        if (!root) {
          root = Node::load(bitstream);
	}

        byte = root->run(bitstream);
      }

      void close() {
        multimap<size_t, Node*> sorted;

	for (pair<char, size_t> entry : counts) {
	  sorted.insert(pair<size_t, Node*>(entry.second, new Leaf(entry.first)));
	}

	while (sorted.size() < 2) {
	  sorted.insert(pair<size_t, Node*>(0, new Leaf('\0')));
	}

	while (sorted.size() > 1) {
	  pair<size_t, Node*> left  = *sorted.begin();
          sorted.erase(sorted.begin());
	  pair<size_t, Node*> right = *sorted.begin();
          sorted.erase(sorted.begin());

	  sorted.insert(pair<size_t, Node*>(left.first + right.first, new InnerNode(left.second, right.second)));
	}

        root = sorted.begin()->second;

        BitStream::Vector path;

        TreeSerialiser serialiser(bitstream);

        root->accept(serialiser);

        translations.swap(serialiser.translations);
      }
  };
  class File {
    public:
      string name;
      size_t size;
      size_t compressed_size;
      size_t offset;

      File(const string& name) : name(name) {
      }

      bool operator< (const File& file) const {
        return name < file.name;
      }

      void compress(ofstream& out) {
	ifstream in(name, ifstream::binary);
	char buffer;
	
	in.seekg(0, in.end);
	size = in.tellg();
	in.seekg(0, in.beg);
        size -= in.tellg();

        BitStream bit(out);
        TreeStream tree(bit);

        in.read(&buffer, 1);

	while (!in.eof()) {
	  tree << buffer;
	  in.read(&buffer, 1);
	}

	tree.close();

	in.clear();
	in.seekg(0, in.beg);

	in.read(&buffer, 1);

	while (!in.eof()) {
	  tree << buffer;

	  in.read(&buffer, 1);
	}

        bit.close();

	compressed_size = bit.size();

	in.close();
      }

      void decompress(ifstream& in) {
        in.seekg(offset, in.beg);

        ofstream out(name, ofstream::binary);

        BitStream bit(in);

	TreeStream tree(bit);

        size_t decompressed_size = 0;

	while (decompressed_size++ < size) {
	  char byte;

	  tree >> byte;

          out << byte;
	}

	out.close();
      }
  };
  class Archive {
    private:
      vector<File> files;
      const string& name;

    public:
      Archive(const string& name) : name(name) {
      }
      
      void bundle() {
        ofstream out(name, ofstream::binary);

        size_t size       = files.size();
	size_t head_start = 0;
	size_t head_end   = 0;

	for (File& file : files) {
	  cout << "File `" << file.name << "`" << flush;

	  file.compress(out);

	  cout << " " << file.size << " >> " << file.compressed_size << endl;
	}

        serialise(size, out);

	head_start = out.tellp();

        for (File& file : files) {
	  out << file.name << endl;

	  serialise(file.size, out);
	  serialise(file.compressed_size, out);
	}

	head_end = out.tellp();

        serialise(head_end - head_start, out);

        out.close();
      }

      void unbundle() {
        ifstream in(name, ifstream::binary);

        size_t size;
	size_t head_size;
	size_t offset = 0;

	in.seekg(-4, in.end);

	deserialise(head_size, in);

	in.seekg(-head_size - 8, in.end);

	deserialise(size, in);

	for (int i = 0; i < size; ++i) {
	  string name;
	  size_t file_size;
	  size_t file_compressed_size;

	  in >> name;
	  in.ignore();

	  deserialise(file_size, in);
	  deserialise(file_compressed_size, in);

	  cout << "File `" << name << "` " << file_size << " >> " << file_compressed_size << endl;

          for (vector<File>::iterator it = files.begin(); it != files.end(); ++it) {
            if (it->name.compare(name) == 0) {
              it->size            = file_size;
              it->compressed_size = file_compressed_size;
              it->offset          = offset;
            }
	  }

	  offset += file_compressed_size;
	}

	for (File& file : files) {
	  in.seekg(file.offset, in.beg);

	  file.decompress(in);
	}

	in.close();
      }

      void add(File& file) {
        files.push_back(file);
      }

    private:
      void serialise(size_t number, ofstream& out) {
        for (int i = 0; i < 32; i += 8) {
	  char byte = (number >> i) & 0xFF;

	  out.write(&byte, 1);
	}
      }
      void deserialise(size_t& number, ifstream& in) {
        number = 0;

        for (int i = 0; i < 32; i += 8) {
          char byte;

	  in.read(&byte, 1);

	  number |= byte << i;
	}
      }
  };
}

int main(int argc, char* argv[]) {
  if (argc < 3) return EXIT_FAILURE;

  deque<string> args;

  for (int i = 1; i < argc; ++i) {
    args.push_back(string(argv[i]));
  }

  bnc::Archive archive(args[1]);

  for (int i = 2; i < args.size(); ++i) {
    bnc::File file(args[i]);

    archive.add(file);
  }

  if (args[0].compare("b") == 0) {
    archive.bundle();
  } else if (args[0].compare("u") == 0) {
    archive.unbundle();
  }

  return EXIT_SUCCESS;
}
