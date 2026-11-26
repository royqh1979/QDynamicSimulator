#include <core/formula/Function.h>

#include <QHash>

namespace qds::core {

const FunctionSignature *findFunctionSignature(const QString &nameKey)
{
    using Category = FunctionCategory;
    static const QHash<QString, FunctionSignature> signatures {
        {"abs", {1, 1, Category::Mathematical}}, {"arccos", {1, 1, Category::Mathematical}},
        {"arcsin", {1, 1, Category::Mathematical}}, {"arctan", {1, 1, Category::Mathematical}},
        {"cos", {1, 1, Category::Mathematical}}, {"exp", {1, 1, Category::Mathematical}},
        {"inf", {0, 0, Category::Mathematical}}, {"int", {1, 1, Category::Mathematical}},
        {"ln", {1, 1, Category::Mathematical}}, {"log10", {1, 1, Category::Mathematical}},
        {"max", {1, 2, Category::Mathematical}}, {"min", {1, 2, Category::Mathematical}},
        {"pi", {0, 0, Category::Mathematical}}, {"sin", {1, 1, Category::Mathematical}},
        {"sqrt", {1, 1, Category::Mathematical}}, {"tan", {1, 1, Category::Mathematical}},
        {"exprnd", {1, 2, Category::Statistical}}, {"lognormal", {2, 3, Category::Statistical}},
        {"normal", {2, 3, Category::Statistical}}, {"poisson", {1, 2, Category::Statistical}},
        {"random", {2, 3, Category::Statistical}}, {"delay", {2, 3, Category::Delay}},
        {"delay1", {2, 3, Category::Delay}}, {"delay3", {2, 3, Category::Delay}},
        {"delayn", {3, 4, Category::Delay}}, {"forcst", {3, 4, Category::Delay}},
        {"smth1", {2, 3, Category::Delay}}, {"smth3", {2, 3, Category::Delay}},
        {"smthn", {3, 4, Category::Delay}}, {"trend", {2, 3, Category::Delay}},
        {"pulse", {2, 3, Category::TestInput}}, {"ramp", {2, 2, Category::TestInput}},
        {"step", {2, 2, Category::TestInput}}, {"dt", {0, 0, Category::Time}},
        {"starttime", {0, 0, Category::Time}}, {"stoptime", {0, 0, Category::Time}},
        {"time", {0, 0, Category::Time}}, {"init", {1, 1, Category::Miscellaneous}},
        {"previous", {2, 2, Category::Miscellaneous}}, {"self", {0, 0, Category::Miscellaneous}},
        {"mean", {1, 1, Category::Array}}, {"rank", {2, 3, Category::Array}},
        {"size", {1, 1, Category::Array}}, {"stddev", {1, 1, Category::Array}},
        {"sum", {1, 1, Category::Array}}, {"if_then_else", {3, 3, Category::Miscellaneous}},
        {"log", {1, 1, Category::Compatibility}}, {"integr", {2, 3, Category::Compatibility}},
        {"smooth", {2, 3, Category::Compatibility}}, {"lookup", {1, 2, Category::Compatibility}}
    };
    const auto found = signatures.find(nameKey);
    return found == signatures.end() ? nullptr : &found.value();
}

} // namespace qds::core
