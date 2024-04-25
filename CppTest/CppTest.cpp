
#include <vector>
#include <string>
#include <string_view>
#include <iostream>
#include <memory>
#include <sstream>
#include <algorithm>
#include <any>


enum class ConfigValueTypes
{
    UNKNOWN = 0,
    NOVALUE = 1,
    INT     = 2,
    FLOAT   = 3,
    STRING  = 4
};


class Config;
class ConfigNode
{
public:
    std::shared_ptr<ConfigNode> next;
    std::shared_ptr<ConfigNode> child;

    virtual std::string GetKey() = 0;
    Config operator[](const std::string& key);
};

class RamNode : public ConfigNode
{
    std::string key;
public:
    RamNode(const std::string& key) : key(key) {}
    virtual std::string GetKey() override { return key; }
};

class YamlParser;
class YamlNode : public ConfigNode
{
    std::string_view key;
    std::string_view value; //Temporary
    friend YamlParser;

public:

    YamlNode(const std::string_view key) : key(key) {}
    virtual std::string GetKey() override { return std::string(key); }
};

class Config
{
    std::shared_ptr<ConfigNode> intern;
public:
    Config(const std::string& key)    {        intern = std::make_shared<RamNode>(key);    }
    Config(std::shared_ptr<ConfigNode> intern) : intern(intern) {}
    Config operator[](const std::string& key) { return (*intern)[key]; }
    std::string GetKey() { return intern->GetKey(); }

    struct Visitor { 
        int depth = 0;
        int width = 0;
        virtual void Visit(Config& node) = 0; 
    };


    void VisitDepthFirst(Visitor& visitor)
    {
        visitor.Visit(*this);
        if (intern->child) {
            Config child(intern->child);
            visitor.depth++;
            child.VisitDepthFirst(visitor);
            visitor.depth--;
        }

        if (intern->next) {
            Config next(intern->next);
            visitor.width++;
            next.VisitDepthFirst(visitor);
            visitor.width--;
        }
    }
};


Config ConfigNode::operator [](const std::string& key) {
    std::shared_ptr<ConfigNode> iter = child;

    // Loop through the direct children to find an existing key.
    while (iter) {
        if (iter->GetKey() == key) {
            return Config(iter);
        }
        iter = iter->next;
    }

    // No key found, create a new ConfigNode for this key and add it to the children.
    auto newNode = std::make_shared<RamNode>(key);
    if (!child) {
        child = newNode;  // Initialize the child if it's not already initialized.
    }
    else {
        iter = child;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = newNode;  // Add the new node to the end of the list of children.
    }

    return Config(newNode);
}

class YamlParser
{
    // Advances idx to beginning of next line, or end of string.
    void AdvanceLine(const std::string_view& input, size_t& idx)
    {
        idx = input.find('\n', idx);
        if (idx == std::string::npos)
            idx = input.length();
        else
            idx++;
    }

    // Takes a single line, withoug advancing idx.
    bool getLine(const std::string_view& input, const size_t& idx, std::string_view& line)
    {
        size_t end = idx;
        AdvanceLine(input, end);
        if (end <= idx)
            return false;
        line = input.substr(idx, end - idx);
        return true;
    }

    // Checks if line contains valid key
    bool checkIfKey(const std::string_view& input, const size_t& idx) {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t i = line.find(':');
        return i != std::string::npos;
    }

    // Returns number of indentations for current node
    size_t countIndents(const std::string_view& input, const size_t& idx)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return std::string::npos;

        return line.find_first_not_of(" \t\n\r");
    }

    bool extractKey(const std::string_view& input, const size_t& idx, std::string_view& key)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t seperator = line.find(':');
        size_t start = line.find_first_not_of(" \t\n\r");

        if (seperator == std::string::npos)
            return false;

        if (start == std::string::npos)
            return false;

        if (start >= seperator)
            return false;

        key = line.substr(start, seperator - start);
        return true;
    }

    bool extractValue(const std::string_view& input, const size_t& idx, std::string_view& value)
    {
        std::string_view line;
        if (!getLine(input, idx, line))
            return false;

        size_t seperator = line.find(':');

        if (seperator == std::string::npos)
            return false;
        else
            seperator++; // Consume :

        if (seperator >= line.length())
            return false;

        value = line.substr(seperator, line.length() - seperator - 1); //Consume \n
        return true;
    }

    bool ensureValidLine(const std::string_view& input, size_t& idx)
    {
        while (!checkIfKey(input, idx) && idx < input.length())
            AdvanceLine(input, idx);
        return idx < input.length();
    }

    std::shared_ptr<YamlNode> Parse(const std::string_view& yaml, size_t& idx)
    {
        // Find begin
        if (!ensureValidLine(yaml, idx))
            return nullptr;

        // IDX points to the beginning of the line, containing the key of this node.
        size_t indentations = countIndents(yaml, idx);
        if (indentations == std::string::npos)
            return nullptr;

        // Get the key
        std::string_view key;
        if (!extractKey(yaml, idx, key))
            return nullptr;   // Coulnt determine the key

        std::shared_ptr<YamlNode> node = std::make_shared<YamlNode>(key);
        //node->indentations = indentations;

        if (extractValue(yaml, idx, node->value))
        {
            //TODO we parse the value here!
        }

        // Advance to next key
        AdvanceLine(yaml, idx);
        if (!ensureValidLine(yaml, idx))
            return node;

        // Check if this is a child
        size_t ind = countIndents(yaml, idx);
        if (ind > indentations)
        {
            node->child = Parse(yaml, idx); //TODO: Do we want to stop here if retuned nullptr?

            // Advance to next key
            if (!ensureValidLine(yaml, idx))
                return node;
        }

        // Check for next node
        ind = countIndents(yaml, idx);
        if (ind == indentations)
        {
            node->next = Parse(yaml, idx); //TODO: Do we want to stop here if retuned nullptr?
        }

        return node;
    }

    class YamlPrintVisitor : public Config::Visitor
    {

    public:

        void Visit(Config& node) override
        {
            std::string indentation(depth * 2, ' ');
            std::cout << indentation << node.GetKey() << ":" << std::endl;
        }
    };

public:

    Config Parse(const std::string_view& yaml)
    {
        size_t idx = 0;
        std::shared_ptr<YamlNode> node = Parse(yaml, idx);
        if (node == nullptr)
            return Config("Root");  //TODO: Do we want to handle this differently?
        return Config(node);
    }

    void Print(Config& config)
    {
        YamlPrintVisitor printVisitor;
        config.VisitDepthFirst(printVisitor);
    }

};








const char* cfg = R"(
DeviceTree:
    MyFirstDevice:
        Compatible: MAXUART
        Baud: 115200
    SecondDevice:
        Compatible: Display
        Uart: MyFirstDevice

)";






int main()
{
    YamlParser parser;

    //Create a config from a Yaml.
    Config config = parser.Parse(cfg);

    //Print the config as a Yaml to the console.
    parser.Print(config);

    std::cout << "\n";
}

