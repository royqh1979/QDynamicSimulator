#pragma once

#include <string>
#include <vector>

namespace qds::core {

enum class VariableType {
    Stock,
    Flow,
    Auxiliary
};

struct Variable {
    std::string name;
    VariableType type = VariableType::Stock;
    std::string equation;
    double x = 0.0;
    double y = 0.0;
};

struct Connection {
    std::string from;
    std::string to;
};

class Model
{
public:
    std::string createVariable(VariableType type, double x = 0.0, double y = 0.0);
    bool addVariable(Variable variable);
    bool removeVariable(const std::string &name);
    bool renameVariable(const std::string &oldName, const std::string &newName);
    bool setEquation(const std::string &name, const std::string &equation);
    bool setPosition(const std::string &name, double x, double y);

    bool addConnection(const std::string &from, const std::string &to);
    bool removeConnection(const std::string &from, const std::string &to);

    const Variable *variable(const std::string &name) const;
    const std::vector<Variable> &variables() const;
    const std::vector<Connection> &connections() const;
    void clear();

private:
    std::string uniqueName(const std::string &base) const;
    bool contains(const std::string &name) const;

    std::vector<Variable> m_variables;
    std::vector<Connection> m_connections;
};

} // namespace qds::core
