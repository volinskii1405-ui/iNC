#include "TestUtil.h"

#include <QGuiApplication>
#include <QTest>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    int status = 0;
    {
        ImageCoreTest t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        MediaCoreTest t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}
