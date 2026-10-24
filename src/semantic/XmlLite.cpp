#include "semantic/XmlLite.h"

#include <fstream>
#include <sstream>

namespace tf::xml {

const Node* Node::child(std::string_view n) const {
  for (auto& c : children)
    if (c->name == n) return c.get();
  return nullptr;
}

std::vector<const Node*> Node::childrenNamed(std::string_view n) const {
  std::vector<const Node*> out;
  for (auto& c : children)
    if (c->name == n) out.push_back(c.get());
  return out;
}

std::string Node::attr(std::string_view n, std::string def) const {
  auto it = attrs.find(std::string(n));
  return it == attrs.end() ? def : it->second;
}

namespace {

std::string decodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') { out += s[i]; continue; }
    size_t semi = s.find(';', i);
    if (semi == std::string_view::npos || semi - i > 10) { out += s[i]; continue; }
    std::string_view ent = s.substr(i + 1, semi - i - 1);
    if (ent == "amp") out += '&';
    else if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "quot") out += '"';
    else if (ent == "apos") out += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      unsigned long cp = ent[1] == 'x' || ent[1] == 'X' ? std::stoul(std::string(ent.substr(2)), nullptr, 16)
                                                        : std::stoul(std::string(ent.substr(1)));
      if (cp < 0x80) out += static_cast<char>(cp);
      else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    } else { out += s.substr(i, semi - i + 1); }
    i = semi;
  }
  return out;
}

struct Parser {
  std::string_view s;
  size_t i = 0;
  std::string err;

  bool eof() const { return i >= s.size(); }
  void skipWs() { while (!eof() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool startsWith(std::string_view p) const { return s.substr(i, p.size()) == p; }

  std::string readName() {
    size_t st = i;
    while (!eof() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == ':' || s[i] == '-' || s[i] == '.')) ++i;
    return std::string(s.substr(st, i - st));
  }

  // Skips <?...?>, <!--...-->, <!DOCTYPE ...>. Returns false if nothing skipped.
  bool skipMisc() {
    if (startsWith("<?")) { size_t e = s.find("?>", i); i = e == std::string_view::npos ? s.size() : e + 2; return true; }
    if (startsWith("<!--")) { size_t e = s.find("-->", i); i = e == std::string_view::npos ? s.size() : e + 3; return true; }
    if (startsWith("<!")) { size_t e = s.find('>', i); i = e == std::string_view::npos ? s.size() : e + 1; return true; }
    return false;
  }

  std::unique_ptr<Node> parseElement() {
    if (!startsWith("<")) { err = "expected '<' at offset " + std::to_string(i); return nullptr; }
    ++i;
    auto node = std::make_unique<Node>();
    node->name = readName();
    if (node->name.empty()) { err = "empty element name at offset " + std::to_string(i); return nullptr; }
    while (true) {
      skipWs();
      if (eof()) { err = "unexpected EOF in tag " + node->name; return nullptr; }
      if (startsWith("/>")) { i += 2; return node; }
      if (startsWith(">")) { ++i; break; }
      std::string an = readName();
      if (an.empty()) { err = "bad attribute in <" + node->name + ">"; return nullptr; }
      skipWs();
      if (!startsWith("=")) { node->attrs[an] = ""; continue; }
      ++i;
      skipWs();
      if (eof()) { err = "EOF in attribute"; return nullptr; }
      char q = s[i];
      if (q != '"' && q != '\'') { err = "unquoted attribute value"; return nullptr; }
      ++i;
      size_t e = s.find(q, i);
      if (e == std::string_view::npos) { err = "unterminated attribute"; return nullptr; }
      node->attrs[an] = decodeEntities(s.substr(i, e - i));
      i = e + 1;
    }
    // content
    while (true) {
      if (eof()) { err = "unexpected EOF inside <" + node->name + ">"; return nullptr; }
      if (startsWith("</")) {
        i += 2;
        std::string close = readName();
        skipWs();
        if (!startsWith(">")) { err = "malformed close tag"; return nullptr; }
        ++i;
        if (close != node->name) { err = "mismatched close tag </" + close + "> for <" + node->name + ">"; return nullptr; }
        return node;
      }
      if (startsWith("<![CDATA[")) {
        size_t e = s.find("]]>", i);
        if (e == std::string_view::npos) { err = "unterminated CDATA"; return nullptr; }
        node->text += s.substr(i + 9, e - i - 9);
        i = e + 3;
        continue;
      }
      if (skipMisc()) continue;
      if (startsWith("<")) {
        auto child = parseElement();
        if (!child) return nullptr;
        node->children.push_back(std::move(child));
        continue;
      }
      size_t e = s.find('<', i);
      if (e == std::string_view::npos) e = s.size();
      node->text += decodeEntities(s.substr(i, e - i));
      i = e;
    }
  }
};

}  // namespace

std::unique_ptr<Node> parse(std::string_view src, std::string* error) {
  Parser p{src, 0, {}};
  // Skip UTF-8 BOM.
  if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF && static_cast<unsigned char>(src[1]) == 0xBB &&
      static_cast<unsigned char>(src[2]) == 0xBF)
    p.i = 3;
  while (true) {
    p.skipWs();
    if (p.eof()) { if (error) *error = "no root element"; return nullptr; }
    if (p.skipMisc()) continue;
    break;
  }
  auto root = p.parseElement();
  if (!root && error) *error = p.err;
  return root;
}

std::unique_ptr<Node> parseFile(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { if (error) *error = "cannot open " + path; return nullptr; }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();
  return parse(content, error);
}

}  // namespace tf::xml
