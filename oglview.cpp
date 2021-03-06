/******************************************************************************
**
**  Copyright 2016 Dale Eason
**  This file is part of DFTFringe
**  is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, version 3 of the License

** DFTFringe is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with DFTFringe.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/
#include "oglview.h"
#include <QtCore>
#include <QLayout>
#include <QColorDialog>
#include <QShortcut>
#include <QMenu>

OGLView::OGLView(QWidget *parent, ContourTools *m_tool,
                 surfaceAnalysisTools *surfTools) :
    QWidget(parent),zoomed(false)
{
    QSettings s;
    m_gl = new GLWidget(this, m_tool, surfTools);
    QHBoxLayout *lh = new QHBoxLayout();
    QVBoxLayout *lv = new QVBoxLayout();
    lv->addLayout(lh);

    fillCB = new QCheckBox("Fill",this);
    lh->addWidget(fillCB);

    bool v = s.value("oglFill", true).toBool();
    fillCB->setChecked(v);
    connect(fillCB, SIGNAL(clicked(bool)), m_gl, SLOT(fillGridChanged(bool)));

    lightingPb = new QPushButton("Lighting",this);
    lh->addWidget(lightingPb);
    connect(lightingPb, SIGNAL(clicked()), m_gl, SLOT(openLightingDlg()));

    QPushButton *showAllPb = new QPushButton("Show All");
    backgroundPb = new QPushButton("Background");
    QColor backg = QColor(s.value("oglBackground", "black").toString());
    backgroundPb->setStyleSheet("  background-color: " + backg.name() +";  border-width: 2px;"
                                "border-color: black;");
    connect(backgroundPb, SIGNAL(clicked(bool)), this, SLOT(setBackground()));
    connect(showAllPb, SIGNAL(clicked()), this, SLOT(showAll()));
    QLabel *lb1 = new QLabel("Vertical Scale:",this);
    lb1->setMaximumHeight(10);
    lh->addWidget(lb1);

    vscale = new QSpinBox(this);
    vscale->setSingleStep(10);
    vscale->setMaximum(1000);
    vscale->setValue(200);
    connect(vscale, SIGNAL(valueChanged(int)), m_gl, SLOT(ogheightMagValue(int)));
    lh->addWidget(vscale);

    QLabel *lb2 = new QLabel("BackWall Scale:");
    lh->addWidget(lb2);
    backWallScaleSB = new QDoubleSpinBox(this);
    backWallScaleSB->setDecimals(3);
    backWallScaleSB->setValue(.125);
    backWallScaleSB->setMinimum(.001);
    backWallScaleSB->setSingleStep(.005);

    lh->addWidget(backWallScaleSB);
    connect(backWallScaleSB, SIGNAL(valueChanged(double)), m_gl, SLOT(backWallScale(double)));
    QLabel *lb3 = new QLabel("Waves",this);
    lb3->setMaximumHeight(10);
    lh->addWidget(lb3);
    lh->addWidget(backgroundPb);
    lh->addWidget(showAllPb);

    lh->addStretch();
    lv->addWidget(m_gl);
    setLayout(lv);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(showContextMenu(QPoint)));
    setMinimumWidth(50);
}
QSize OGLView::sizeHint() const{
    return QSize(300,300);
}

void OGLView::zoom(){
    zoomed = !zoomed;
    emit zoomMe(zoomed);
}

void OGLView::showContextMenu(const QPoint &pos)
{
    // Handle global position
    QPoint globalPos = mapToGlobal(pos);

    // Create menu and insert some actions
    QMenu myMenu;
    QString txt = (zoomed)? "Restore to MainWindow" : "FullScreen";
    myMenu.addAction(txt,  this, SLOT(zoom()));

    // Show context menu at handling position
    myMenu.exec(globalPos);
}
void OGLView::setBackground(){
    QColorDialog dlg(m_gl->m_background,this);
    QColor c = dlg.getColor();
    m_gl->setBackground(c);
    backgroundPb->setStyleSheet("  background-color: " + c.name() +";  border-width: 2px;"
                                "border-color: black;");
}

void OGLView::showAll(){
    emit showAll3d( m_gl);

}
