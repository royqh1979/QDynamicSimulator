#include <core/formula/Evaluation.h>

#include <QElapsedTimer>
#include <QTextStream>

#include <atomic>
#include <cstdlib>
#include <new>
#include <utility>

using namespace qds::core;

namespace {

std::atomic<std::size_t> allocationCount {0};
std::atomic<std::size_t> allocationBytes {0};

struct LegacyArrayValue {
    QVector<QString> dimensions;
    QVector<std::size_t> shape;
    QVector<double> values;
};

using LegacyValue = std::variant<double, LegacyArrayValue>;

struct LegacyEvaluationResult {
    std::optional<LegacyValue> value;
    QString error;
    SourceSpan errorSpan;
};

LegacyEvaluationResult legacyResult(const LegacyArrayValue &input)
{
    return {LegacyValue(input), {}, {}};
}

EvaluationResult sharedResult(const ArrayValue &input)
{
    return {Value(input), {}, {}};
}

template <typename T>
void consume(const T &value)
{
#if defined(__GNUC__)
    asm volatile("" : : "g"(&value) : "memory");
#else
    static volatile const void *address;
    address = &value;
#endif
}

template <typename Operation>
void measure(QTextStream &out, const QString &name, int iterations, int arraySize, Operation operation)
{
    QElapsedTimer timer;
    timer.start();
    operation();
    const qint64 elapsedNs = timer.nsecsElapsed();
    out << name << ',' << iterations << ',' << arraySize << ','
        << QString::number(elapsedNs / 1e6, 'f', 6) << ','
        << QString::number(elapsedNs / double(iterations), 'f', 3) << '\n';
}

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
    const int iterations = argc > 1 ? QString(argv[1]).toInt() : 1000000;
    const int arraySize = argc > 2 ? QString(argv[2]).toInt() : 16;
    if (iterations <= 0 || arraySize <= 0)
        return 2;

    const QVector<QString> dimensions {"n"};
    const QVector<std::size_t> shape {static_cast<std::size_t>(arraySize)};
    const QVector<double> values(arraySize, 1.0);
    const LegacyArrayValue legacyInput {dimensions, shape, values};
    const ArrayValue sharedInput {dimensions, shape, values};

    QTextStream out(stdout);
    out << "scenario,iterations,array_size,total_ms,ns_per_iteration\n";
    measure(out, "empty_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const LegacyArrayValue value {dimensions, shape, {}};
            consume(value);
        }
    });
    measure(out, "empty_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const ArrayValue value {dimensions, shape, {}};
            consume(value);
        }
    });
    measure(out, "copy_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const LegacyArrayValue copy = legacyInput;
            consume(copy);
        }
    });
    measure(out, "copy_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const ArrayValue copy = sharedInput;
            consume(copy);
        }
    });
    measure(out, "publish_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const LegacyArrayValue value {dimensions, shape, values};
            consume(value);
        }
    });
    measure(out, "publish_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const ArrayValue value {dimensions, shape, values};
            consume(value);
        }
    });
    measure(out, "variant_copy_legacy", iterations, arraySize, [&] {
        const LegacyValue input = legacyInput;
        for (int index = 0; index < iterations; ++index) {
            const LegacyValue copy = input;
            consume(copy);
        }
    });
    measure(out, "variant_copy_shared", iterations, arraySize, [&] {
        const Value input = sharedInput;
        for (int index = 0; index < iterations; ++index) {
            const Value copy = input;
            consume(copy);
        }
    });
    measure(out, "variant_publish_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const LegacyValue value = legacyInput;
            consume(value);
        }
    });
    measure(out, "variant_publish_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const Value value = sharedInput;
            consume(value);
        }
    });
    measure(out, "result_publish_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const LegacyEvaluationResult result = legacyResult(legacyInput);
            consume(result);
        }
    });
    measure(out, "result_publish_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            const EvaluationResult result = sharedResult(sharedInput);
            consume(result);
        }
    });
    measure(out, "mutable_access_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            LegacyArrayValue value {dimensions, shape, values};
            for (int element = 0; element < arraySize; ++element)
                consume(value.values);
        }
    });
    measure(out, "mutable_access_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            ArrayValue value {dimensions, shape, values};
            for (int element = 0; element < arraySize; ++element)
                consume(value.values());
        }
    });
    measure(out, "full_publish_legacy", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            QVector<double> output;
            output.reserve(arraySize);
            for (int element = 0; element < arraySize; ++element)
                output.push_back(values[element] + 1.0);
            const LegacyArrayValue result {dimensions, shape, std::move(output)};
            consume(result);
        }
    });
    measure(out, "full_publish_shared", iterations, arraySize, [&] {
        for (int index = 0; index < iterations; ++index) {
            QVector<double> output;
            output.reserve(arraySize);
            for (int element = 0; element < arraySize; ++element)
                output.push_back(values[element] + 1.0);
            const ArrayValue result {dimensions, shape, std::move(output)};
            consume(result);
        }
    });
    return 0;
}
