# Coding convention

## Naming convention

- External variables: capital letters
- Static global variables: lower case letters
- Function names: TypeName_verb or verb_noun
- Type names: camel case with the first letter capitalised, e.g. CamelCase

## Indentation style

Before submitting your pull request, you need to run your code through the
formatter. You need to enter your ``builddir``, then run:

    meson compile format

## Doxygen documentation

To generate Doxygen documentation (which can be very helpful for development),
you need to enter your ``builddir``, then run:

    meson compile doxygen

The Doxygen documentation will be generated in the ``doxygen`` under the root
of the repository.
