# Vensim Variable Types

来源：https://www.vensim.com/documentation/ref_variable_types.html

本文件保存通过页面读取工具取得的正文，供离线核对。

Explaining variables is often difficult without reference to equations, but it is useful to have a complete categorization and specify conventions before we talk about equations. Vensim is designed so that what a variable is and does can be determined by the way it is defined or used. There are no forward declarations of variables and no added markers indicating what a variable is. The following list describes the eleven variable types used in Vensim.

NOTE Vensim is not case sensitive. You can enter variables with any mix of upper and lower case letters. Vensim will retain the case, but recognize the variable even when used with other capitalization patterns.

- **Auxiliary.** Any dynamic variable that is computed from other variables at a given time. Auxiliaries are typically the most numerous variable type. An auxiliary variable has an expression involving other variables in its equation.
- **Constant.** A variable whose value does not change over time. Constants have numbers on the right side of their equations or can be defined using the GET XLS CONSTANTS function or the TABBED ARRAY function. A constant can be temporarily changed prior to simulating a model.
- **Data.** These have values that change over time, but do not depend on other model variables (except possibly other data). Data use an empty equation to denote raw data or a := equation to indicate the way in which they are derived. If a variable is used in a model, but not defined, it will be assumed exogenous and therefore treated as a Data variable. This makes it easy to run sections of a model without writing new equations.
- **Group.** Groups are not really variables, but a way to group different variables together. They have no values, but can be used to access collections of other variable types. Groups appear enclosed in special markers which consist of four (4) or more asterisks ****, or are defined by typing the name into the Group selector in the Equation Edit tool. Group names are shown preceded by a period.
- **Initial.** Like a constant, except that it is the result of combining different variables at initialization time. Initials all have INITIAL or REINITIAL equations.
- **Level.** The dynamic variables in the model. Levels all have INTEG equations.
- **Lookup.** Nonlinear functions with numerical parameters (where the parameters are the x- and y-axis values). They are defined in equations beginning with a left parenthesis ( and ending with a right parenthesis ).
- **String Variable.** String Variables (also called String Constants) take on a character string as a value. They are useful in MESSAGE functions and as labels in Venapps.
- **Subscript Element.** An element of a Subscript Range. These identify the meaning of specific values of a subscript. The Subscript Elements appear in a Subscript Range equation.
- **Subscript Range.** Rather than repeating the same equation over and over, it is possible to define a variable with a subscript that has a range of values. Subscript Ranges are defined with a special equation that begins with a colon.
- **Time Base.** Like an Auxiliary, but with some special output and data interpretation features. These must use the TIME BASE equation as described in Chapter 4.
- **Units.** Units are defined as additional information about a model variable and can be used to check the model for dimensional consistency. Units are entered in the units field of an equation.
- **Unchangeable Constants.** These are Constants that can’t be changed during simulation experiments. For example the number of days per year would sensibly be defined as an Unchangeable Constant. Equations for Unchangeable Constants are the same as those for Constants but use a double equal sign == for assignment.

Constants and Unchangeable Constants are almost the same. The only difference is that the value for an Unchangeable Constant is determined from its equations, or read from a spreadsheet when a model is checked, and never changed after that. If you include an Unchangeable Constant in a .cin file you will get an error message. Unchangeable Constants are not given sliders in SyntheSim and are not included in Constant lists for Sensitivity, Optimization or Setting of Parameters.
