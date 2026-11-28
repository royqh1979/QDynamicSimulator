#include <core/Document.h>
#include <core/formula/Evaluation.h>
#include <core/formula/Formula.h>

#include <QElapsedTimer>
#include <QTextStream>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>

using namespace qds::core;

namespace {

std::atomic<std::size_t> allocationCount {0};
std::atomic<std::size_t> allocationBytes {0};

struct Scenario {
    QString name;
    QString equation;
    bool arrayResult = true;
};

} // namespace

void *operator new(std::size_t size)
{
    if (void *memory = std::malloc(size)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocationBytes.fetch_add(size, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    if (void *memory = std::malloc(size)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocationBytes.fetch_add(size, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char **argv)
{
    const int iterations = argc > 1 ? QString(argv[1]).toInt() : 100000;
    const int arraySize = argc > 2 ? QString(argv[2]).toInt() : 16;
    if (iterations <= 0 || arraySize <= 1)
        return 2;

    Document document;
    document.defineDimension("N", arraySize);
    for (int index = 1; index <= 16; ++index) {
        MacroDefinitionData definition;
        definition.displayName = "Identity" + QString::number(index);
        definition.parameters = {{"x", {}}};
        definition.equation = index == 1 ? QStringLiteral("x")
            : QStringLiteral("Identity%1(x)").arg(index - 1);
        if (!document.defineMacro(definition))
            return 3;
    }

    QVector<double> inputValues(arraySize);
    for (int index = 0; index < arraySize; ++index)
        inputValues[index] = index + 1.0;
    const ArrayValue input {{"N"}, {static_cast<std::size_t>(arraySize)}, inputValues};
    EvaluationContext context;
    context.readDimension = [&document](const QString &name) { return document.findDimension(name); };
    context.readVariable = [&input](const QString &name) -> std::optional<Value> {
        return name == "input" ? std::optional<Value>(input) : std::nullopt;
    };
    context.readMacro = [&document](const QString &name, int arguments, int) -> std::optional<MacroEvaluation> {
        const MacroDefinition *definition = document.findMacro(name, arguments, 1);
        return definition ? std::optional<MacroEvaluation>({definition}) : std::nullopt;
    };

    const QVector<Scenario> scenarios {
        {"array_identity", "input"},
        {"array_macro_nested_16", "Identity16(input)"},
        {"array_broadcast", "input + 1"},
        {"array_subscript", "input[2]", false}
    };
    QTextStream out(stdout);
    out << "scenario,iterations,array_size,total_ms,ns_per_iteration,checksum,allocations,allocated_bytes\n";
    for (const Scenario &scenario : scenarios) {
        const Formula formula = analyzeFormula(scenario.equation);
        if (!formula.root() || !formula.issues().isEmpty())
            return 4;
        allocationCount.store(0, std::memory_order_relaxed);
        allocationBytes.store(0, std::memory_order_relaxed);
        QElapsedTimer timer;
        timer.start();
        double checksum = 0.0;
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const EvaluationResult result = evaluateFormula(formula.root(), context);
            if (!result.value) {
                QTextStream(stderr) << scenario.name << ':' << result.error << '\n';
                return 5;
            }
            if (scenario.arrayResult) {
                const auto *array = std::get_if<ArrayValue>(&*result.value);
                if (!array)
                    return 6;
                for (const double value : array->values())
                    checksum += value;
            } else {
                const auto *scalar = std::get_if<double>(&*result.value);
                if (!scalar)
                    return 7;
                checksum += *scalar;
            }
        }
        const qint64 elapsedNs = timer.nsecsElapsed();
        if (!std::isfinite(checksum))
            return 8;
        out << scenario.name << ',' << iterations << ',' << arraySize << ','
            << QString::number(elapsedNs / 1e6, 'f', 6) << ','
            << QString::number(elapsedNs / double(iterations), 'f', 3) << ','
            << QString::number(checksum / iterations, 'g', 17) << ','
            << allocationCount.load(std::memory_order_relaxed) << ','
            << allocationBytes.load(std::memory_order_relaxed) << '\n';
    }
    return 0;
}
