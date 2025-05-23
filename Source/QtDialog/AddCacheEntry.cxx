/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "AddCacheEntry.h"

#include "QCMakeSizeType.h"
#include <QCompleter>
#include <QMetaProperty>

static int const NumTypes = 4;
static int const DefaultTypeIndex = 0;
static QByteArray const TypeStrings[NumTypes] = { "BOOL", "PATH", "FILEPATH",
                                                  "STRING" };
static QCMakeProperty::PropertyType const Types[NumTypes] = {
  QCMakeProperty::BOOL, QCMakeProperty::PATH, QCMakeProperty::FILEPATH,
  QCMakeProperty::STRING
};

AddCacheEntry::AddCacheEntry(QWidget* p, QStringList const& varNames,
                             QStringList const& varTypes)
  : QWidget(p)
  , VarNames(varNames)
  , VarTypes(varTypes)
{
  this->setupUi(this);
  for (auto const& elem : TypeStrings) {
    this->Type->addItem(elem);
  }
  QWidget* cb = new QCheckBox();
  QWidget* path = new QCMakePathEditor();
  QWidget* filepath = new QCMakeFilePathEditor();
  QWidget* string = new QLineEdit();
  this->StackedWidget->addWidget(cb);
  this->StackedWidget->addWidget(path);
  this->StackedWidget->addWidget(filepath);
  this->StackedWidget->addWidget(string);
  AddCacheEntry::setTabOrder(this->Name, this->Type);
  AddCacheEntry::setTabOrder(this->Type, cb);
  AddCacheEntry::setTabOrder(cb, path);
  AddCacheEntry::setTabOrder(path, filepath);
  AddCacheEntry::setTabOrder(filepath, string);
  AddCacheEntry::setTabOrder(string, this->Description);
  QCompleter* completer = new QCompleter(this->VarNames, this);
  this->Name->setCompleter(completer);
  connect(
    completer,
    static_cast<void (QCompleter::*)(QString const&)>(&QCompleter::activated),
    this, &AddCacheEntry::onCompletionActivated);
}

QString AddCacheEntry::name() const
{
  return this->Name->text().trimmed();
}

QVariant AddCacheEntry::value() const
{
  QWidget* w = this->StackedWidget->currentWidget();
  if (qobject_cast<QLineEdit*>(w)) {
    return static_cast<QLineEdit*>(w)->text();
  }
  if (qobject_cast<QCheckBox*>(w)) {
    return static_cast<QCheckBox*>(w)->isChecked();
  }
  return QVariant();
}

QString AddCacheEntry::description() const
{
  return this->Description->text();
}

QCMakeProperty::PropertyType AddCacheEntry::type() const
{
  int idx = this->Type->currentIndex();
  if (idx >= 0 && idx < NumTypes) {
    return Types[idx];
  }
  return Types[DefaultTypeIndex];
}

QString AddCacheEntry::typeString() const
{
  int idx = this->Type->currentIndex();
  if (idx >= 0 && idx < NumTypes) {
    return TypeStrings[idx];
  }
  return TypeStrings[DefaultTypeIndex];
}

void AddCacheEntry::onCompletionActivated(QString const& text)
{
  cm_qsizetype idx = this->VarNames.indexOf(text);
  if (idx != -1) {
    QString vartype = this->VarTypes[idx];
    for (int i = 0; i < NumTypes; i++) {
      if (TypeStrings[i] == vartype) {
        this->Type->setCurrentIndex(i);
        break;
      }
    }
  }
}
