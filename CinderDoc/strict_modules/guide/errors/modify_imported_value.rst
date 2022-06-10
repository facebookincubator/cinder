StrictModuleModifyImportedValueException
########################################

  <dict> from module <module> is modified by <other module>; this is
  prohibited.

Strict modules only allow a module to modify values that are defined/created
within the defining module.  This prevents one module from having side effects
that would impact another module or non-determinism based upon the order of
imports.

For additional information see the section on
:doc:`/strict_modules/guide/limitations/ownership`.

For guidance on how to fix this see :doc:`/strict_modules/guide/conversion/external_modification`.
