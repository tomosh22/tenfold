#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tf::xml {

// A tiny, dependency-free XML reader: enough for MSBuild project files
// (.vcxproj/.props/.sln-adjacent XML). Elements, attributes, text, comments,
// CDATA, basic entities. No DTD/namespaces beyond keeping prefixes in names.
struct Node {
  std::string name;
  std::map<std::string, std::string> attrs;
  std::vector<std::unique_ptr<Node>> children;
  std::string text;  // concatenated character data directly inside this node

  const Node* child(std::string_view name) const;
  std::vector<const Node*> childrenNamed(std::string_view name) const;
  std::string attr(std::string_view name, std::string def = {}) const;
};

std::unique_ptr<Node> parse(std::string_view src, std::string* error = nullptr);
std::unique_ptr<Node> parseFile(const std::string& path, std::string* error = nullptr);

}  // namespace tf::xml
