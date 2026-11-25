#include <qds/core/Formula.h>

#include <QElapsedTimer>
#include <QString>
#include <QTextStream>

using namespace qds::core;

int main(int argc, char **argv)
{
    const int count = argc > 1 ? QString(argv[1]).toInt() : 1000;
    if (count <= 0)
        return 2;
    QElapsedTimer timer;
    timer.start();
    std::size_t totalNodes = 0;
    for (int index = 0; index < count; ++index) {
        const std::string equation = "a" + std::to_string(index) + "*b" + std::to_string(index)
            + "+ABS(c" + std::to_string(index) + ")-IF d" + std::to_string(index)
            + " THEN e" + std::to_string(index) + " ELSE f" + std::to_string(index);
        FormulaAnalysis analysis = analyzeFormula(equation);
        if (!analysis.ast || !analysis.issues.empty()) {
            QTextStream(stderr) << "Formula analysis failed at index " << index << "\n";
            return 1;
        }
        totalNodes += formulaNodeCount(analysis.ast.get());
    }
    const qint64 elapsedNs = timer.nsecsElapsed();
    if (totalNodes == 0)
        return 3;
    QTextStream(stdout) << "operation,count,total_ms,per_formula_us\n"
                        << "formula_analyze," << count << "," << elapsedNs / 1e6 << ","
                        << elapsedNs / 1000.0 / count << "\n"
                        << "formula_nodes," << totalNodes << ",\n";
    return 0;
}
