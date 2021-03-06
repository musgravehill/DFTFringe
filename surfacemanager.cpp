/******************************************************************************
**
**  Copyright 2016 Dale Eason
**  This file is part of DFTFringe
**  is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation version 3 of the License

** DFTFringe is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with DFTFringe.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/
#include "surfacemanager.h"
#include <limits>
#include <cmath>
#include <QWidget>
#include <opencv/cv.h>
#include <QtWidgets/QMessageBox>
#include <QDebug>
#include "mainwindow.h"
#include "zernikeprocess.h"
#include "zernikedlg.h"
#include <QMessageBox>
#include <QTextStream>
#include <QHash>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_symbol.h>
#include <qwt_legend.h>
#include <qwt_text.h>
#include <qwt_plot_renderer.h>
#include <qwt_interval_symbol.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_scale_engine.h>
#include <algorithm>
#include <vector>
#include "zernikeprocess.h"
#include <QTimer>
#include <qprinter.h>
#include "rotationdlg.h"
#include <qwt_scale_draw.h>
#include "zernikes.h"
#include <qwt_abstract_scale.h>
#include <qwt_plot_histogram.h>
#include "savewavedlg.h"
#include "simulationsview.h"
#include "standastigwizard.h"
#include "subtractwavefronatsdlg.h"
#include <QTextDocument>
#include <qwt_plot_marker.h>
#include <qwt_symbol.h>
#include <QTabWidget>
#include <QPrintDialog>
#include <QSplitter>
#include "settingsgeneral.h"
#include "foucaultview.h"
QMutex mutex;
int inprocess = 0;

void expandBorder(wavefront *wf){

    double cx, cy;
    cx = wf->m_outside.m_center.rx();
    cy = wf->m_outside.m_center.ry();
    double rad = wf->m_outside.m_radius;

    for (int y = 0; y < wf->data.rows; ++y){
        for (int x = 0; x < wf->data.cols; ++x){
            int dx = x - cx;
            int dy = y - cy;
            double rho = sqrt(dx * dx + dy * dy);
            if (rho/rad > 1.){
                double rx = rad* (dx/rho) + cx;
                double ry = rad * (dy/rho) + cy;
                double drx = x - rx;
                double dry = y - ry;
                double nx = rx - drx;
                double ny = ry - dry;
                //qDebug() << x << y << rx << ry << nx << ny;
                wf->nulledData(y,x) = wf->nulledData(ny,nx);
            }

        }
    }
}

class wftNameScaleDraw: public QwtScaleDraw
{
public:
    QVector<wavefront*> names;
    int currentNdx;
    wftNameScaleDraw(QVector<wavefront*> nameList, int nx)
    {
        names = nameList;
        currentNdx = nx;
        setTickLength( QwtScaleDiv::MajorTick, 10 );
        setTickLength( QwtScaleDiv::MinorTick, 1 );
        setTickLength( QwtScaleDiv::MediumTick, 5 );

        setLabelRotation( -45.0 );
        setLabelAlignment( Qt::AlignLeft | Qt::AlignVCenter );

        setSpacing( 5 );
    }
    virtual QwtText label( double value ) const
    {
        if (value >= names.size())
            return QString("");
        QString n = names[int(((int)value + currentNdx) % names.size())]->name;
        QStringList l = n.split("/");
        if (l.size() > 1)
            n = l[l.size()-2] + "/"+ l[l.size()-1];
        return n;
    }
};

class StrehlScaleDraw: public QwtScaleDraw
{
public:
    StrehlScaleDraw()
    {

    }
    virtual QwtText label( double value ) const
    {
        double st =(2. *M_PI * value);
        st *= st;
        const double  e = 2.7182818285;
        double strl = pow(e,-st);
        return QString().sprintf("%6.3lf",strl);
    }
};

class ZernScaleDraw: public QwtScaleDraw
{
public:
    QVector<QString> m_names;
    ZernScaleDraw(QVector<QString> names)

    {
        m_names = names;
        setTickLength( QwtScaleDiv::MajorTick, 10 );
        setTickLength( QwtScaleDiv::MinorTick, 1 );
        setTickLength( QwtScaleDiv::MediumTick, 5 );

        setLabelRotation( -60.0 );
        setLabelAlignment( Qt::AlignLeft | Qt::AlignVCenter );

        setSpacing( 5 );
    }

    virtual QwtText label( double value ) const
    {
        if (value == 2.){
            QwtText x( QString("wave front RMS"));
            x.setColor(Qt::red);

            return x;
        }
        if (value < 4)
            return QString("");
        if (value > m_names.size() +3)
            return QString("");

        QwtText t(QString(m_names[int(value-4)]));
        if (value == 6.)
            t.setColor(Qt::red);
        return t;

    }
};
// --- CONSTRUCTOR ---
surfaceGenerator::surfaceGenerator(SurfaceManager *sm) :
    m_sm(sm),
    m_zg(0)
{

}

// --- DECONSTRUCTOR ---
surfaceGenerator::~surfaceGenerator() {
    // free resources
}

// --- PROCESS ---
// Start processing data.
void surfaceGenerator::process(int wavefrontNdx, SurfaceManager *sm) {
    // allocate resources using new here
    m_sm = sm;
    mutex.lock();
    ++inprocess;
    mutex.unlock();
    wavefront *wf = m_sm->m_wavefronts[wavefrontNdx];



    if (wf->dirtyZerns){
        if (mirrorDlg::get_Instance()->isEllipse()){
            wf->nulledData = wf->data.clone();
            if (m_sm->m_GB_enabled){

                    //expandBorder(wf);
                    cv::GaussianBlur( wf->nulledData.clone(), wf->workData, cv::Size( m_sm->m_gbValue, m_sm->m_gbValue ),0,0);
            }
            else {
                wf->workData = wf->data.clone();
            }
            wf->InputZerns = std::vector<double>(Z_TERMS, 0);
            wf->dirtyZerns = false;
            emit finished( wavefrontNdx);

            return;
        }

        //compute zernike values
        zernikeProcess &zp = *zernikeProcess::get_Instance();

        zp.unwrap_to_zernikes(*wf);

        ((MainWindow*)m_sm->parent())-> zernTablemodel->setValues(wf->InputZerns);

        // null out desired terms.
        wf->nulledData = zp.null_unwrapped(*wf, wf->InputZerns, zernEnables,0,Z_TERMS   );
        wf->dirtyZerns = false;

    }
    wf->workData = wf->nulledData.clone();
    if (m_sm->m_GB_enabled){

            expandBorder(wf);
            cv::GaussianBlur( wf->nulledData.clone(), wf->workData, cv::Size( m_sm->m_gbValue, m_sm->m_gbValue ),0,0);

    }

    QMutexLocker lock(&mutex);

    emit finished( wavefrontNdx);
}
cv::Mat SurfaceManager::computeWaveFrontFromZernikes(int wx, int wy, std::vector<double> &zerns, QVector<int> zernsToUse){
    double rad = getCurrent()->m_outside.m_radius;
    double xcen = (wx-1)/2, ycen = (wy-1)/2;

    cv::Mat result = cv::Mat::zeros(wy,wx, CV_64F);

    double rho;

    std::vector<bool> &en = zernEnables;
    mirrorDlg *md = mirrorDlg::get_Instance();
    zernikePolar &zpolar = *zernikePolar::get_Instance();
    for (int i = 0; i <  wx; ++i)
    {
        double x1 = (double)(i - (xcen)) / rad;
        for (int j = 0; j < wy; ++j)
        {
            double y1 = (double)(j - (ycen )) /rad;
            rho = sqrt(x1 * x1 + y1 * y1);

            if (rho <= 1.)
            {
                double S1 = 0;
                double theta = atan2(y1,x1);
                zpolar.init(rho,theta);
                for (int ii = 0; ii < zernsToUse.size(); ++ii) {
                    int z = zernsToUse[ii];

                    if ( z == 3 && m_surfaceTools->m_useDefocus){
                        S1 -= m_surfaceTools->m_defocus * zpolar.zernike(z,rho,theta);
                    }
                    else {
                        if (en[z]){
                            if (z == 8 && md->doNull)
                                S1 +=    md->z8 * zpolar.zernike(z,rho, theta);

                            S1 += zerns[z] * zpolar.zernike(z,rho, theta);
                        }
                    }
                }
                result.at<double>(j,i) = S1;
            }
            else
            {
                result.at<double>(j,i) = 0 ;
            }
        }
    }
    //cv::imshow("zernbased", result);
    //cv::waitKey(1);
    return result;
}
SurfaceManager *SurfaceManager::m_instance = 0;
SurfaceManager *SurfaceManager::get_instance(QObject *parent, surfaceAnalysisTools *tools,
                                             ProfilePlot *profilePlot, ContourPlot *contourPlot,
                                             GLWidget *glPlot, metricsDisplay *mets){
    if (m_instance == 0){
        m_instance = new SurfaceManager(parent, tools, profilePlot, contourPlot, glPlot, mets);
    }
    return m_instance;
}

SurfaceManager::SurfaceManager(QObject *parent, surfaceAnalysisTools *tools,
                               ProfilePlot *profilePlot, ContourPlot *contourPlot,
                               GLWidget *glPlot, metricsDisplay *mets): QObject(parent),
    m_surfaceTools(tools),m_profilePlot(profilePlot), m_contourPlot(contourPlot),
    m_oglPlot(glPlot), m_metrics(mets),
    m_gbValue(21),m_GB_enabled(false),m_currentNdx(-1),insideOffset(0),
    outsideOffset(0),m_askAboutReverse(true), workToDo(0), m_wftStats(0)
{
    m_simView = SimulationsView::getInstance(0);
    pd = new QProgressDialog();
    connect (this,SIGNAL(progress(int)), pd, SLOT(setValue(int)));
    m_generatorThread = new QThread;
    surfaceGenerator *sg = new surfaceGenerator(this);
    connect(sg,SIGNAL(showMessage(QString)), parent, SLOT(showMessage(QString)));
    sg->moveToThread(m_generatorThread);
    m_profilePlot->setWavefronts(&m_wavefronts);
    // create a timer for surface change update to all non current wave fronts
    m_waveFrontTimer = new QTimer(this);
    m_toolsEnableTimer = new QTimer(this);
    // setup signal and slot
    connect(m_waveFrontTimer, SIGNAL(timeout()),this, SLOT(backGroundUpdate()));
    connect(m_toolsEnableTimer, SIGNAL(timeout()), this, SLOT(enableTools()));
    connect(m_generatorThread, SIGNAL(finished()), m_generatorThread, SLOT(deleteLater()));
    connect(this, SIGNAL(generateSurfacefromWavefront(int, SurfaceManager *)), sg, SLOT(process(int, SurfaceManager *)));
    connect(sg, SIGNAL(finished(int)), this, SLOT(surfaceGenFinished(int)));
    m_generatorThread->start();

    connect(m_surfaceTools, SIGNAL(waveFrontClicked(int)), this, SLOT(waveFrontClickedSlot(int)));
    connect(m_surfaceTools, SIGNAL(wavefrontDClicked(const QString &)), this, SLOT(wavefrontDClicked(const QString &)));
    connect(m_surfaceTools, SIGNAL(centerMaskValue(int)),this, SLOT(centerMaskValue(int)));
    connect(m_surfaceTools, SIGNAL(outsideMaskValue(int)),this, SLOT(outsideMaskValue(int)));
    connect(m_surfaceTools, SIGNAL(surfaceSmoothGBEnabled(bool)), this, SLOT(surfaceSmoothGBEnabled(bool)));
    connect(m_surfaceTools, SIGNAL(surfaceSmoothGBValue(int)), this, SLOT(surfaceSmoothGBValue(int)));
    connect(m_surfaceTools, SIGNAL(wftNameChanged(int,QString)), this, SLOT(wftNameChanged(int,QString)));
    connect(this, SIGNAL(nameChanged(QString, QString)), m_surfaceTools, SLOT(nameChanged(QString,QString)));
    connect(m_metrics, SIGNAL(recomputeZerns()), this, SLOT(computeZerns()));
    connect(m_surfaceTools, SIGNAL(defocusChanged()), this, SLOT(computeZerns()));
    connect(this, SIGNAL(currentNdxChanged(int)), m_surfaceTools, SLOT(currentNdxChanged(int)));
    connect(this, SIGNAL(deleteWavefront(int)), m_surfaceTools, SLOT(deleteWaveFront(int)));
    connect(m_surfaceTools, SIGNAL(deleteTheseWaveFronts(QList<int>)), this, SLOT(deleteWaveFronts(QList<int>)));
    connect(m_surfaceTools, SIGNAL(average(QList<int>)),this, SLOT(average(QList<int>)));
    connect(m_surfaceTools, SIGNAL(doxform(QList<int>)),this, SLOT(transfrom(QList<int>)));
    connect(m_surfaceTools, SIGNAL(invert(QList<int>)),this,SLOT(invert(QList<int>)));
    connect(this, SIGNAL(enableControls(bool)),m_surfaceTools, SLOT(enableControls(bool)));
    connect(mirrorDlg::get_Instance(),SIGNAL(recomputeZerns()), this, SLOT(computeZerns()));
    connect(mirrorDlg::get_Instance(),SIGNAL(obstructionChanged()), this, SLOT(ObstructionChanged()));
    QSettings settings;
    m_GB_enabled = settings.value("GBlur", true).toBool();
    m_gbValue = settings.value("GBValue", 21).toInt();
    //useDemoWaveFront();

}

SurfaceManager::~SurfaceManager(){}



void SurfaceManager::makeMask(int waveNdx){
    int width = m_wavefronts[waveNdx]->data.cols;
    int height = m_wavefronts[waveNdx]->data.rows;
    double xm,ym;
    xm = m_wavefronts[waveNdx]->m_outside.m_center.x();
    ym = m_wavefronts[waveNdx]->m_outside.m_center.y();
    double radm = m_wavefronts[waveNdx]->m_outside.m_radius + outsideOffset - 2;
    double rado = m_wavefronts[waveNdx]->m_inside.m_radius;
    double cx = m_wavefronts[waveNdx]->m_inside.m_center.x();
    double cy = m_wavefronts[waveNdx]->m_inside.m_center.y();
    cv::Mat mask = cv::Mat::zeros(height,width,CV_8U);
    mirrorDlg &md = *mirrorDlg::get_Instance();
    double rx = radm;
    double rx2 = rx * rx;
    double ry = rx * md.m_verticalAxis/md.diameter;
    double ry2 = ry * ry;
    for (int y = 0; y < height; ++y){
        for (int x = 0; x < width; ++x){
            if (!mirrorDlg::get_Instance()->isEllipse()){
                double dx = (double)(x - xm)/(radm);
                double dy = (double)(y - ym)/(radm);
                if (sqrt(dx * dx + dy * dy) <= 1.)
                    mask.at<uchar>(y,x) = 255;
            }
            else {
                if (fabs(x -xm) > rx || fabs(y -ym) > ry)
                    continue;
                double v = pow(x - xm, 2)/rx2 + pow(y - ym, 2)/ry2;
                if (v <= 1)
                    mask.at<uchar>(y,x) = 255;
            }
        }
    }

    if (rado > 0) {
        rado += insideOffset + 2;
        for (int y = 0; y < height; ++y){
            for (int x = 0; x < width; ++x){
                double dx = (double)(x - (cx))/(rado);
                double dy = (double)(y - (cy))/(rado);
                if (sqrt(dx * dx + dy * dy) < 1.)
                    mask.at<uchar>(y,x) = 0;
            }
        }

    }
    m_wavefronts[waveNdx]->mask = mask;
    m_wavefronts[waveNdx]->workMask = mask.clone();

        // add central obstruction

        double r = md.obs * (2. * radm)/md.diameter;
        r/= 2.;
        if (r > 0){
            cv::Mat m = m_wavefronts[waveNdx]->workMask;
            circle(m,Point(m.cols/2,m.cols/2),r, Scalar(0),-1);
        }
}
void SurfaceManager::wftNameChanged(int ndx, QString name){
    m_wavefronts[ndx]->name = name;

}

void SurfaceManager::sendSurface(wavefront* wf){
    emit currentNdxChanged(m_currentNdx);
    computeMetrics(wf);

    m_contourPlot->setSurface(wf);
    m_profilePlot->setSurface(wf);
    m_oglPlot->setSurface(wf);
    m_simView->setSurface(wf);
    foucaultView::get_Instance()->setSurface(wf);

    QFile fn(wf->name);
    QFileInfo fileInfo(fn.fileName());
    QString filename(fileInfo.fileName());
    ((MainWindow*)(parent()))->setWindowTitle(filename);
}
void SurfaceManager::ObstructionChanged(){
    if (m_wavefronts.size() > 0)
    outsideMaskValue(outsideOffset);// use this function to trigger the mask making and recomputing.
}

void SurfaceManager::centerMaskValue(int val){
    insideOffset = val;
    double mmPerPixel = getCurrent()->diameter/(2 *( m_wavefronts[m_currentNdx]->m_outside.m_radius-1));
    m_surfaceTools->m_centerMaskLabel->setText(QString().sprintf("%6.2lf mm",mmPerPixel* val));
    makeMask(m_currentNdx);
    wavefront *wf = m_wavefronts[m_currentNdx];
    wf->dirtyZerns = true;
    wf->wasSmoothed = false;
    //emit generateSurfacefromWavefront(m_currentNdx, this);
    m_waveFrontTimer->start(500);

}

void SurfaceManager::outsideMaskValue(int val){
    outsideOffset = val;
    double mmPerPixel = m_wavefronts[m_currentNdx]->diameter/(2 * (m_wavefronts[m_currentNdx]->m_outside.m_radius));
    m_surfaceTools->m_edgeMaskLabel->setText(QString().sprintf("%6.2lf mm",mmPerPixel* val));
    makeMask(m_currentNdx);
    wavefront *wf = m_wavefronts[m_currentNdx];
    wf->dirtyZerns = true;
    wf->wasSmoothed = false;
    //emit generateSurfacefromWavefront(m_currentNdx, this);
    m_waveFrontTimer->start(500);

}
void SurfaceManager::useDemoWaveFront(){
    int wx = 640;
    int wy = wx;
    double rad = (double)(wx-1)/2.;
    double xcen = rad,ycen = rad;
    double rho;
    mirrorDlg *md = mirrorDlg::get_Instance();
    cv::Mat result = cv::Mat::zeros(wx,wx, CV_64F);
    zernikePolar &zpolar = *zernikePolar::get_Instance();
    for (int i = 0; i <  wx; ++i)
    {
        double x1 = (double)(i - (xcen)) / rad;
        for (int j = 0; j < wy; ++j)
        {
            double y1 = (double)(j - (ycen )) /rad;
            rho = sqrt(x1 * x1 + y1 * y1);
            double theta = atan2(y1,x1);
            zpolar.init(rho,theta);

            if (rho <= 1.)
            {
                double S1 = md->z8 * -.9 * zpolar.zernike(8,rho,theta) + .02* zpolar.zernike(9, rho,theta);

                result.at<double>(j,i) = S1;
            }
            else
            {
                result.at<double>(j,i) = 0 ;
            }
        }
    }




    createSurfaceFromPhaseMap(result,
                              CircleOutline(QPointF(xcen,ycen),rad),
                              CircleOutline(QPointF(0,0),0),
                              QString("Demo"));
}

void SurfaceManager::waveFrontClickedSlot(int ndx)
{
    if (inprocess != 0)
        return;
    m_currentNdx = ndx;
    sendSurface(m_wavefronts[ndx]);
}
void SurfaceManager::deleteWaveFronts(QList<int> list){
    if (inprocess != 0)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    foreach(int ndx, list ){
        m_currentNdx = ndx;

        deleteCurrent();
    }
    QApplication::restoreOverrideCursor();
}

void SurfaceManager::wavefrontDClicked(const QString & name){
    QString end = name;
    end.replace("\u2026","");
    for (int i = 0; i < m_wavefronts.size(); ++i){
        if (m_wavefronts[i]->name.endsWith(end)){
            m_currentNdx = i;
            sendSurface(m_wavefronts[i]);
            break;
        }
    }
}

void SurfaceManager::surfaceSmoothGBValue(int value){

    if (inprocess != 0)
        return;

    if (value %2 == 0) ++value;  // make sure blur radius is always odd;
    m_gbValue = value;

    QSettings settings;
    settings.setValue("GBValue", m_gbValue);
    double rad = 320.;
    if (m_wavefronts.size() != 0)
        rad = m_wavefronts[m_currentNdx]->m_outside.m_radius-1;
    mirrorDlg *md = mirrorDlg::get_Instance();
    double mmPerPixel = md->diameter/(2 * rad);
    m_surfaceTools->setBlurText(QString().sprintf("%6.2lf mm",m_gbValue* mmPerPixel));
    if (m_wavefronts.size() == 0)
        return;
    m_wavefronts[m_currentNdx]->GBSmoothingValue = 0;
    //emit generateSurfacefromWavefront(m_currentNdx, this);

    m_waveFrontTimer->start(1000);
}
void SurfaceManager::surfaceSmoothGBEnabled(bool b){
    if (inprocess != 0)
        return;
    m_GB_enabled = b;

    QSettings settings;
    settings.setValue("GBlur", m_GB_enabled);
    double rad = 320.;

    if (m_wavefronts.size() != 0)
        rad = m_wavefronts[m_currentNdx]->m_outside.m_radius-1;

    mirrorDlg *md = ((MainWindow*)parent())->m_mirrorDlg;
    double mmPerPixel = md->diameter/(2 * rad);
    m_surfaceTools->setBlurText(QString().sprintf("%6.2lf mm",m_gbValue* mmPerPixel));
    if (m_wavefronts.size() == 0)
        return;
    //emit generateSurfacefromWavefront(m_currentNdx, this);
    m_waveFrontTimer->start(500);
}

void SurfaceManager::computeMetrics(wavefront *wf){
    mirrorDlg *md = mirrorDlg::get_Instance();
    cv::Scalar mean,std;
    cv::meanStdDev(wf->workData,mean,std,wf->workMask);
    wf->mean = mean.val[0] * md->lambda/550.;
    wf->std = std.val[0]* md->lambda/550.;

    double mmin;
    double mmax;

    minMaxIdx(wf->workData, &mmin,&mmax);

    wf->min = mmin * md->lambda/550.;
    wf->max = mmax * md->lambda/550.;


    ((MainWindow*)(parent()))->zernTablemodel->setValues(wf->InputZerns);

    ((MainWindow*)(parent()))->updateMetrics(*wf);

}


void SurfaceManager::computeZerns()
{
    QList<int> doThese =  m_surfaceTools->SelectedWaveFronts();
    workToDo = doThese.size();
    mirrorDlg &md = *mirrorDlg::get_Instance();
    foreach(int ndx , doThese){
        wavefront &wf = *m_wavefronts[ndx];
        wf.diameter = md.diameter;
        wf.roc = md.roc;
        wf.lambda = md.lambda;
    }

    m_waveFrontTimer->start(500);
}

void SurfaceManager::writeWavefront(QString fname, wavefront *wf, bool saveNulled){
    std::ofstream file((fname.toStdString().c_str()));

    if (!file.is_open()) {
        QMessageBox::warning(0, tr("Write wave front"),
                             tr("Cannot write file %1: ")
                             .arg(fname));
        return;
    }
    file << wf->data.cols << std::endl << wf->data.rows << std::endl;
    for (int row = wf->data.rows - 1; row >=0; --row){
        for (int col = 0; col < wf->data.cols ; ++col){
            if (saveNulled)
                file << wf->workData(row,col) << std::endl;
            else {
                file << wf->data(row,col) << std::endl;

            }
        }
    }

    file << "outside ellipse " <<
                   wf->m_outside.m_center.x()
         << " " << wf->m_outside.m_center.y()
         << " " << wf->m_outside.m_radius
         << " "  << wf->m_outside.m_radius << std:: endl;

    if (wf->m_inside.m_radius > 0){
        file << "obstruction ellipse " << wf->m_inside.m_center.x()
         << " " << wf->m_inside.m_center.y()
         << " " << wf->m_inside.m_radius
         << " " << wf->m_inside.m_radius << std:: endl;
    }

    file << "DIAM " << wf->diameter << std::endl;
    file << "ROC " << wf->roc << std::endl;
    file << "Lambda " << wf->lambda << std::endl;
    mirrorDlg &md = *mirrorDlg::get_Instance();
    if (md.isEllipse()){
        file << "ellipse_vertical_axis " << md.m_verticalAxis;
    }
}

void SurfaceManager::SaveWavefronts(bool saveNulled){
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<int> list = m_surfaceTools->SelectedWaveFronts();
    if (list.size() <= 1 && m_wavefronts.size() > 0) {
        QSettings settings;
        QString lastPath = settings.value("lastPath","").toString();
        QString fileName = m_wavefronts[m_currentNdx]->name;
        QFileInfo fileinfo(fileName);
        QString file = fileinfo.baseName();
        fileName = QFileDialog::getSaveFileName(0,
             tr("Write wave font file"), lastPath + "/" + file,
             tr("wft (*.wft)"));
        if (fileName.isEmpty())
            return;
        if (QFileInfo(fileName).suffix().isEmpty()) { fileName.append(".wft"); }
        QString lastDir = QFileInfo(fileName).absoluteDir().path();
        settings.setValue("lastPath", lastDir);
        writeWavefront(fileName, m_wavefronts[m_currentNdx], saveNulled);

    }
    else if (list.size() > 1){
        QSettings settings;
        QString path = settings.value("lastPath").toString();
        QFile fn(path);
        QFileInfo info(fn.fileName());
        QString dd = info.dir().absolutePath();

        QString dir = QFileDialog::getExistingDirectory(0, tr("Open Directory")
                                                        ,path,
                                                        QFileDialog::ShowDirsOnly
                                                        | QFileDialog::DontResolveSymlinks);
        if (dir == "")
            return;
        // find last file in dir

        QProgressDialog progress("Saving wavefront files", "Cancel", 0, list.size(), 0);
        progress.setWindowModality(Qt::WindowModal);
        progress.setValue(0);
        for (int i = 0; i < list.size(); ++i){
            progress.setValue(i+1);

            wavefront *wf = m_wavefronts[list[i]];
            progress.setLabelText(wf->name);

            QString fname = wf->name;
            QStringList fnparts = fname.split("/");
            if (fnparts.size() > 1)
                fname = fnparts[fnparts.size()-1];
            if (QFileInfo(fname).suffix().isEmpty()) { fname.append(".wft");}
            QString fullPath = dir + QDir::separator() + fname;
            QFileInfo info;
            // check if file exists and if yes: Is it really a file and no directory?
            int cnt = 1;
            if (info.exists(fullPath)) {
                if (QMessageBox::question(0,"file alread exists", fullPath +" already exists. Do you want to overwrite it?") ==
                        QMessageBox::No)
                    continue;

            }
            qApp->processEvents();
            writeWavefront(fullPath, wf, saveNulled);


        }
    }
    QApplication::restoreOverrideCursor();
}
void SurfaceManager::createSurfaceFromPhaseMap(cv::Mat phase, CircleOutline outside, CircleOutline center, QString name){

    wavefront *wf;

    if (m_wavefronts.size() >0 && (m_currentNdx == 0 &&  m_wavefronts[0]->name == "Demo")){
        wf = m_wavefronts[0];
        emit nameChanged(wf->name, name);
        wf->name = name;
        wf->dirtyZerns = true;
    }
    else {
        wf = new wavefront();
        m_wavefronts << wf;

        wf->name = name;

        m_surfaceTools->addWaveFront(wf->name);
        m_currentNdx = m_wavefronts.size()-1;
    }
    wf->m_outside = outside;
    wf->m_inside = center;
    wf->data = phase;
    mirrorDlg *md = mirrorDlg::get_Instance();
    wf->diameter = md->diameter;
    wf->lambda = md->lambda;
    wf->roc = md->roc;
    wf->dirtyZerns = true;
    wf->wasSmoothed = false;
    m_currentNdx = m_wavefronts.size()-1;
    makeMask(m_currentNdx);
    m_surface_finished = false;
    emit generateSurfacefromWavefront(m_currentNdx, this);
    while (!m_surface_finished) {qApp->processEvents();}
    // check for swapped conic value
    if (md->cc * wf->InputZerns[8] < 0.){
        bool reverse = false;
        if (m_askAboutReverse){
            if (QMessageBox::Yes == QMessageBox::question(0,"should invert?","Wavefront seems inverted.  Do you want to invert it?"))
            {
               reverse = true;
                m_askAboutReverse = false;
            }else
            {
                reverse = false;
            }
        }
        else {
            reverse = true;
        }
        if (reverse){
            wf->data *= -1;
            wf->dirtyZerns = true;
            wf->wasSmoothed = false;
            m_surface_finished = false;
            emit generateSurfacefromWavefront(m_currentNdx, this);
            while (!m_surface_finished) {qApp->processEvents();}
        }
    }
    m_surfaceTools->select(m_currentNdx);
    emit showTab(2);

}

bool SurfaceManager::loadWavefront(const QString &fileName){
    emit enableControls(false);
    bool mirrorParamsChanged = false;
    std::ifstream file(fileName.toStdString().c_str());
    if (!file) {
        QString b = "Can not read file " + fileName + " " +strerror(errno);
        QMessageBox::warning(NULL, tr("Read Wavefront File"),b);
    }
    wavefront *wf;

    if (m_currentNdx == 0 &&  m_wavefronts[0]->name == "Demo"){
        wf = m_wavefronts[0];
        emit nameChanged(wf->name, fileName);
        wf->name = fileName;
        wf->dirtyZerns = true;
    }
    else {
        wf = new wavefront();
        m_wavefronts << wf;

        wf->name = fileName;

        m_surfaceTools->addWaveFront(wf->name);
        m_currentNdx = m_wavefronts.size()-1;
    }
    double width;
    double height;
    file >> width;
    file >> height;

    cv::Mat data(height,width, CV_64F,0.);

    for( size_t y = 0; y < height; y++ ) {
        for( size_t x = 0; x < width; x++ ) {
            file >> data.at<double>(height - y-1,x);
        }
    }

    string line;
    QString l;
    mirrorDlg *md = mirrorDlg::get_Instance();

    double xm = width/2.,ym = (height)/2.,
            radm = min(xm,ym)-2 ,
            roc = md->roc,
            lambda = md->lambda,
            diam = md->diameter;
    double xo = width/2., yo = height/2., rado = 0;

    string dummy;
    while (getline(file, line)) {
        l = QString::fromStdString(line);
        std::istringstream iss(line);
        if (l.startsWith("outside")) {
            QStringList sl = l.split(" ");
            xm = sl[2].toDouble();
            radm = sl[4].toDouble();
            ym = sl[3].toDouble();
            continue;
        }
        if (l.startsWith("DIAM")){
            iss >> dummy >> diam;
            continue;
        }
        if (l.startsWith("ROC")){
            iss >> dummy >> roc;
            continue;
        }
        if (l.startsWith("Lambda")){
            iss >> dummy >> lambda;
            continue;
        }
        if (l.startsWith("obstruction")){
            qDebug() << line.c_str();
            iss >> dummy >> dummy >> xo >> yo >> rado;
            continue;
        }
        if (l.startsWith("ellipse_vertical_axis")){
            md->m_outlineShape = ELLIPSE;
            iss >> dummy >> md->m_verticalAxis;
        }
    }

    wf->m_outside = CircleOutline(QPointF(xm,height - ym), radm);
    if (rado == 0){
        xo = xm;
        yo = ym;
    }
    if (md->isEllipse()){
        wf->m_outside = CircleOutline(QPointF(xm,height - ym), xm -2);
    }
    wf->m_inside = CircleOutline(QPoint(xo,yo), rado);

    wf->diameter = diam;

    if (lambda != md->lambda){
        QString message("The interferogram wavelength (");
        message += QString().sprintf("%6.3lf", lambda) +
                ") Of the wavefront does not match the config value of " + QString().sprintf("%6.3lf\n",md->lambda) +
                "Do you want to make the config match?";

        sync.lock();
        emit showMessage(message);
        pauseCond.wait(&sync);
        sync.unlock();
        if (messageResult == QMessageBox::Yes){
            md->newLambda(QString::number(lambda));
        }
        else {
            lambda = md->diameter;
            mirrorParamsChanged = true;
        }
    }

    if (roundl(diam * 10) != roundl(md->diameter* 10))
    {
        QString message("The mirror diameter (");
        message += QString().sprintf("%6.3lf",diam) +
                ") Of the wavefront does not match the config value of " + QString().sprintf("%6.3lf\n",md->diameter) +
                "Do you want to make the config match?";

        sync.lock();
        emit showMessage(message);
        pauseCond.wait(&sync);
        sync.unlock();
        if (messageResult == QMessageBox::Yes){
            emit diameterChanged(diam);
        }
        else {
            diam = md->diameter;
            mirrorParamsChanged = true;
        }

    }

    if (roundl(roc * 10.) != roundl(md->roc * 10.))
    {
        QString message("The mirror roc (");
        message += QString().sprintf("%6.3lf",roc) +
                ") Of the wavefront does not match the config value of " + QString().sprintf("%6.3lf\n",md->roc) +
                "Do you want to make the config match?";
        //qDebug() << message;
        sync.lock();

        emit showMessage(message);
        pauseCond.wait(&sync);
        sync.unlock();
        if (messageResult == QMessageBox::Yes){
            emit rocChanged(roc);
            mirrorParamsChanged = true;
        }
        else {
            roc = md->roc;

        }
    }

    wf->data= data;
    wf->roc = roc;
    wf->lambda = lambda;
    wf->wasSmoothed = false;

    makeMask(m_currentNdx);
    m_surface_finished = false;
    emit generateSurfacefromWavefront(m_currentNdx, this);
    while (!m_surface_finished){qApp->processEvents();}
    m_surfaceTools->select(m_currentNdx);
    return mirrorParamsChanged;
}
void SurfaceManager::deleteCurrent(){
    if (m_wavefronts.size() == 0)
        return;
    if (m_wavefronts.length()) {
        emit deleteWavefront(m_currentNdx);
        delete m_wavefronts[m_currentNdx];
        m_wavefronts.removeAt(m_currentNdx);

        if (m_currentNdx > 0)
            --m_currentNdx;
    }

    if (m_wavefronts.length() > 0) {

        sendSurface(m_wavefronts[m_currentNdx]);
    }
    else useDemoWaveFront();

    emit currentNdxChanged(m_currentNdx);
}

void SurfaceManager::processSmoothing(){
    if (m_wavefronts.size() == 0)
        return;
    wavefront *wf = m_wavefronts[m_currentNdx];
    if (m_GB_enabled){
        if (wf->wasSmoothed != m_GB_enabled || wf->GBSmoothingValue != m_gbValue) {

            cv::GaussianBlur( wf->nulledData.clone(), wf->workData, cv::Size( m_gbValue, m_gbValue ),0,0);
        }
    }
    else if (wf->wasSmoothed == true) {
        wf->workData = wf->nulledData.clone();
    }

    sendSurface(wf);
}

void SurfaceManager::next(){
    if (m_wavefronts.size() == 0)
        return;
    if (m_currentNdx < m_wavefronts.length()-1)
        ++m_currentNdx;
    else
        m_currentNdx = 0;
    sendSurface(m_wavefronts[m_currentNdx]);


}

void SurfaceManager::previous(){
    if (m_wavefronts.size() == 0)
        return;
    if (m_currentNdx > 0) {
        --m_currentNdx;
    }
    else
        m_currentNdx = m_wavefronts.length()-1;

    sendSurface(m_wavefronts[m_currentNdx]);
}
QVector<int> histo(const std::vector<double> data, int bins, double min, double max){
    QVector<int> h(bins, 0);
    double interval = (max - min)/bins;
    for (unsigned int i = 0; i < data.size(); ++i){
        double to = interval;
        for (int j = 0; j <  bins; ++j){
            if (data[i] < to) {
                ++h[j];
                break;
            }
            to += interval;
        }
    }
    return h;
}
#include "statsview.h"
void SurfaceManager::saveAllWaveFrontStats(){

    if (m_wavefronts.size() == 0)
        return;
        statsView * sv = new statsView(this);
        sv->show();
        return;
}
void SurfaceManager::enableTools(){

    m_toolsEnableTimer->stop();
    if (inprocess == 0){
        m_surfaceTools->setEnabled(true);

    }
}

void SurfaceManager::surfaceGenFinished(int ndx) {
    m_toolsEnableTimer->start(1000);



    if (workToDo > 0)
        emit progress(++workProgress);
    mutex.lock();
    --inprocess;

    mutex.unlock();
    if (inprocess == 0)
        sendSurface(m_wavefronts[m_currentNdx]);
    else
        computeMetrics(m_wavefronts[ndx]);

    m_surface_finished = true;
    if (workProgress == workToDo)
        workToDo = 0;

}

// Update all surfaces since some control has changed.  Skip current surface it has already been done
void SurfaceManager::backGroundUpdate(){

    m_waveFrontTimer->stop();
    workProgress = 0;

    m_surfaceTools->setEnabled(false);
    QList<int> doThese =  m_surfaceTools->SelectedWaveFronts();
    workToDo = doThese.size();
    pd->setLabelText("Updating Selected Surfaces");
    pd->setRange(0,doThese.size());
    foreach (int i, doThese){
        m_wavefronts[i]->dirtyZerns = true;
        m_wavefronts[i]->wasSmoothed = false;
        emit generateSurfacefromWavefront(i, this);
    }
}


void SurfaceManager::average(QList<int> list){
    QList<wavefront *> wflist;
    for (int i = 0; i < list.size(); ++i){
        wflist.append(m_wavefronts[list[i]]);
    }
    average(wflist);
}
#include "ccswappeddlg.h"
void SurfaceManager::average(QList<wavefront *> wfList){

    // check that all the cc have the same sign
    bool sign = wfList[0]->InputZerns[8] < 0;
    bool someReversed = false;
    bool needsUpdate = false;
    foreach (wavefront *wf, wfList){
        if ((wf->InputZerns[8] < 0) !=  sign)
        {
            someReversed = true;
            break;
        }
    }
    if (someReversed){
        CCSwappedDlg dlg;
        if (dlg.exec()){
            foreach(wavefront *wf, wfList){
                if ((wf->InputZerns[8] < 0 && dlg.getSelection() == NEGATIVE) ||
                    (wf->InputZerns[8] > 0 && dlg.getSelection() == POSITIVE))
                {
                    wf->data *= -1;
                    wf->dirtyZerns = true;
                    wf->wasSmoothed = false;
                    m_surface_finished = false;
                    needsUpdate = true;
                }
            }
        }
        else {
            return;
        }
    }
    // normalize the size to the most common size

    QHash<QString,int> sizes;
    for (int i = 0; i < wfList.size(); ++i){
        QString size;
        size.sprintf("%d %d",wfList[i]->workData.rows, wfList[i]->workData.cols);
        if (*sizes.find(size))
        {
            ++sizes[size];
        }
        else
            sizes[size] = 1;
    }
    int max = 0;
    QString maxkey;
    foreach(QString v, sizes.keys()){
        int a = sizes[v];
        if (a > max) {
            max = a;
            maxkey = v;
        }
    }
    int rrows, rcols;

    QTextStream s(&maxkey);

    s >> rrows >> rcols;
    cv::Mat mask = m_wavefronts[0]->workMask.clone();
    cv::resize(mask,mask,Size(rcols,rrows));
    cv::Mat sum = cv::Mat::ones(rrows,rcols, m_wavefronts[m_currentNdx]->workData.type());
    for (int j = 0; j < wfList.size(); ++j){
        cv::Mat resized = wfList[j]->data.clone();
        if (wfList[j]->data.rows != rrows || wfList[j]->data.cols != rcols){
            cv::resize(wfList[j]->data, resized, Size(rcols, rrows));
        }
        sum += resized;
    }
    sum = sum/wfList.size();
    wavefront *wf = new wavefront();
    *wf = *wfList[0];
    wf->data = sum;
    wf->mask = mask;
    wf->workMask = mask.clone();
    m_wavefronts << wf;
    wf->wasSmoothed = false;
    wf->name = "Average.wft";
    wf->dirtyZerns = true;
    m_surfaceTools->addWaveFront(wf->name);
    m_currentNdx = m_wavefronts.size()-1;
    m_surface_finished = false;
    emit generateSurfacefromWavefront(m_currentNdx, this);
    while (!m_surface_finished) {qApp->processEvents();}
    if (needsUpdate)
        m_waveFrontTimer->start(1000);
}

void SurfaceManager::rotateThese(double angle, QList<int> list){
    workToDo = list.size();
    workProgress = 0;
    pd->setLabelText("Rotating Wavefronts");
    pd->setRange(0, list.size());
    for (int i = 0; i < list.size(); ++i) {
        wavefront *oldWf = m_wavefronts[list[i]];
        QString newName;
        QStringList l = oldWf->name.split('.');
        newName.sprintf("%s_%s%05.1lf",l[0].toStdString().c_str(), (angle >= 0) ? "CW":"CCW", fabs(angle) );
        newName += ".wft";
        wavefront *wf = new wavefront();
        *wf = *m_wavefronts[list[0]];
        //emit nameChanged(wf->name, newName);
        wf->name = newName;
        cv::Mat tmp = oldWf->data.clone();
        wf->data = tmp;
        m_wavefronts << wf;
        m_surfaceTools->addWaveFront(wf->name);
        m_currentNdx = m_wavefronts.size()-1;
        cv::Point2f ptCp(tmp.cols*0.5, tmp.rows*0.5);
        cv::Mat M = cv::getRotationMatrix2D(ptCp, angle, 1.0);
        cv::warpAffine(tmp, wf->data , M, tmp.size(), cv::INTER_CUBIC);
        wf->m_inside = oldWf->m_inside;
        wf->m_outside = oldWf->m_outside;
        double rad = -angle * M_PI/180.;
        double sina = sin(rad);
        double cosa = cos(rad);
        double sx = wf->m_inside.m_center.x() - wf->m_outside.m_center.x();
        double sy = wf->m_inside.m_center.y() - wf->m_outside.m_center.y();

        wf->m_inside.m_center.rx() = sx * cosa - sy * sina + wf->m_outside.m_center.x();
        wf->m_inside.m_center.ry() = sx * sina + sy * cosa + wf->m_outside.m_center.y();


        makeMask(m_currentNdx);
        wf->workMask = wf->mask.clone();

        wf->dirtyZerns = true;
        wf->wasSmoothed = false;
        m_surface_finished = false;
        emit generateSurfacefromWavefront(m_currentNdx,this);
        while (!m_surface_finished) {qApp->processEvents();}

    }
}
void SurfaceManager::subtract(wavefront *wf1, wavefront *wf2, bool use_null){

    int size1 = wf1->data.rows * wf1->data.cols;
    int size2 = wf2->data.rows * wf2->data.cols;
    cv::Mat resize = wf2->data.clone();
    if (size1 != size2) {
        cv::resize(resize,resize, cv::Size(wf1->data.cols, wf1->data.rows));
    }
    cv::Mat result = wf1->data - resize;
    wavefront *resultwf = new wavefront;
    *resultwf = *wf1;
    resultwf->data = result.clone();

    m_wavefronts << resultwf;
    m_currentNdx = m_wavefronts.size() -1;
    makeMask(m_currentNdx);

    QStringList n1 = wf1->name.split("/");
    QStringList n2 = wf2->name.split("/");
    resultwf->name = n1[n1.size()-1] + "-" + n2[n2.size()-1];
    m_surfaceTools->addWaveFront(resultwf->name);
    resultwf->dirtyZerns = true;
    resultwf->wasSmoothed = false;

    m_surface_finished = false;
    if (!use_null){
        resultwf->useSANull = false;
    }
    emit generateSurfacefromWavefront(m_currentNdx,this);
    while (!m_surface_finished){qApp->processEvents();}

}

void SurfaceManager::subtractWavefronts(){
    QList<QString> list;
    for (int i = 0; i < m_wavefronts.size(); ++i){
        list.append(m_wavefronts[i]->name);
    }
    subtractWavefronatsDlg dlg(list);
    if (dlg.exec()){
        int ndx2 = dlg.getSelected();
        if (ndx2 == -1){
            QMessageBox::warning(0,"Warning", "No wavefront was selected.");
            return;
        }
        wavefront *wf1 = m_wavefronts[m_currentNdx];
        wavefront *wf2 = m_wavefronts[ndx2];
        subtract(wf1,wf2, dlg.use_null);
    }
}

void SurfaceManager::transfrom(QList<int> list){
    RotationDlg dlg(list);
    connect(&dlg, SIGNAL(rotateTheseSig(double, QList<int>)), this, SLOT(rotateThese( double, QList<int>)));
    dlg.exec();

}
void SurfaceManager::invert(QList<int> list){
    workToDo = list.size();
    workProgress = 0;
    pd->setLabelText("Inverting Wavefronts");
    pd->setRange(0, list.size());
    for (int i = 0; i < list.size(); ++i) {
        m_wavefronts[list[i]]->data *= -1;
        m_wavefronts[list[i]]->dirtyZerns = true;
        m_wavefronts[list[i]]->wasSmoothed = false;
    }
    m_waveFrontTimer->start(1000);
}

#include "wftexaminer.h"
wftExaminer *wex;
void SurfaceManager::inspectWavefront(){
    wex = new wftExaminer(m_wavefronts[m_currentNdx]);
    wex->show();
}


#include <sstream>



textres SurfaceManager::Phase2(QList<rotationDef *> list, QList<int> inputs, int avgNdx ){
    QTextEdit *editor = new QTextEdit;

    QTextDocument *doc = editor->document();
    textres results;
    results.Edit = editor;
    const int Width = 400 * .9;
    const int Height = 300 * .9;
    QImage contour(Width ,Height, QImage::Format_ARGB32 );

    QPrinter printer(QPrinter::HighResolution);
    printer.setColorMode( QPrinter::Color );
    printer.setFullPage( true );
    printer.setOutputFileName( "stand.pdf" );
    printer.setOutputFormat( QPrinter::PdfFormat );
    //printer.setResolution(85);
    printer.setPaperSize(QPrinter::A4);

    QPainter painterPDF( &printer );
    cv::Mat xastig = cv::Mat::zeros(list.size(), 1, CV_64F);
    cv::Mat yastig = cv::Mat::zeros(list.size(), 1, CV_64F);
    cv::Mat standxastig = cv::Mat::zeros(list.size(), 1, CV_64F);
    cv::Mat standyastig = cv::Mat::zeros(list.size(), 1, CV_64F);
    cv::Mat standastig = cv::Mat::zeros(list.size(), 1, CV_64F);
    QVector<cv::Mat> standwfs;
    QVector<double> astigMag;
    editor->resize(printer.pageRect().size());
    doc->setPageSize(printer.pageRect().size());
    cv::Mat standavg = cv::Mat::zeros(m_wavefronts[inputs[0]]->workData.size(), CV_64F);
    cv::Mat standavgZernMat = cv::Mat::zeros(m_wavefronts[inputs[0]]->workData.size(), CV_64F);
    for (int i = 0; i < list.size(); ++i){

        // rotate the average to match the input
        QList<int> toRotate;
        toRotate.append(avgNdx);
        int ndx = m_wavefronts.size(); // index of result of rotation
        m_surface_finished = false;
        rotateThese(-list[i]->angle,toRotate);
        while(!m_surface_finished){qApp->processEvents();}
        m_surface_finished = false;
        subtract(m_wavefronts[inputs[i]], m_wavefronts[ndx],false);
        ++ndx;      // now ndx point to the stand only wavefront
        while(!m_surface_finished){qApp->processEvents();}
        cv::Mat resized = m_wavefronts[ndx]->workData.clone();
        if (standavg.cols != m_wavefronts[ndx]->workData.cols || standavg.rows != m_wavefronts[ndx]->workData.rows){
            cv::resize(m_wavefronts[ndx]->workData, resized, Size(standavg.cols, standavg.rows));
        }
        standavg += resized;
        //create contour of astig
        double xa = standxastig.at<double>(i,0) = m_wavefronts[ndx]->InputZerns[4];
        double ya = standyastig.at<double>(i,0) = m_wavefronts[ndx]->InputZerns[5];
        double mag = sqrt(xa * xa + ya * ya);
        standastig.at<double>(i,0) = mag;
        astigMag.push_back(sqrt(mag));
        QVector<int> zernsToUse;
        zernsToUse << 4 << 5;
        for (int ii = 9; ii < Z_TERMS; ++ii)
            zernsToUse << ii;

        cv::Mat m = computeWaveFrontFromZernikes(m_wavefronts[inputs[0]]->data.cols,m_wavefronts[inputs[0]]->data.rows,
                m_wavefronts[ndx]->InputZerns, zernsToUse );
        standavgZernMat += m;
        standwfs << m;
        xastig.at<double>(i,0) = m_wavefronts[inputs[i]]->InputZerns[4];
        yastig.at<double>(i,0) = m_wavefronts[inputs[i]]->InputZerns[5];
    }
    double smin,smax;
    cv::minMaxIdx(standavg,&smin, &smax);

    standavg /= list.size();

    cv::minMaxIdx(standavgZernMat,&smin, &smax);

    standavgZernMat = standavgZernMat/list.size();
    cv::minMaxIdx(standavgZernMat,&smin, &smax);

    cv::Scalar standMean,standStd;
    cv::Scalar standXMean,standXStd;
    cv::Scalar standYMean, standyStd;
    cv::meanStdDev(standastig,standMean,standStd);
    cv::meanStdDev(standxastig,standXMean,standXStd);
    cv::meanStdDev(standyastig,standYMean,standyStd);

    double ymin,standymin, ymax,standymax;
    double xmax,standxmax, xmin,standxmin;

    minMaxIdx(standxastig, &standxmin,&standxmax);
    minMaxIdx(standyastig, &standymin,&standymax);
    minMaxIdx(xastig, &xmin,&xmax);
    minMaxIdx(yastig, &ymin,&ymax);

    QwtPlot *pl1 = new QwtPlot();
    QVector<QwtPlotCurve *> curves;
    QVector<QwtPlotMarker *> markers;
    QVector<QwtPlot *> astigPlots;

    QwtPlotRenderer renderer;
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasFrame );
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend);
    QPainter painter( &contour );

    QString html = "<html><head/><body><h1>Test Stand Astig Analysis</h1>"
            "<h3>Step 2. Stand induced astig at each rotation position:</h3>";
    html.append( "<table  style='ds margin-top:0px; margin-bottom:0px; margin-left:10px; margin-right:10px;'"
                 " width='70%' cellspacing='1' cellpadding='1'>"

                 "<tr><b><td>rotation angle</td><td>X astig</td><td>Y astig</td><td> magnitude</td><td>astig angle Degrees</td></b></tr>");
    for (int i = 0; i < list.size(); ++i){
        double xval = standxastig.at<double>(i,0);
        double yval = standyastig.at<double>(i,0);
        html.append("<tr><td><align = 'right'> " + QString().number(list[i]->angle,'f',3 ) + "</td>" );
        html.append("<td>" + QString().number(xval,'f',3) + "</td>"
                    "<td>" + QString().number(yval,'f',3) + "</td>"
                    "<td>" + QString().number(sqrt(pow(xval,2) + pow(yval,2)),'f',3) + "</td>"
                    "<td>" + QString().number((180./ M_PI) * atan2(yval,xval),'f',3) + "</td></right></tr>");
    }
    html.append("<tr></b><td>MEAN</b></td><td>" + QString().number(standXMean[0],'f',3) + "</td><td>" +
                QString().number(standYMean[0],'f',3) + "</td><td>" + QString().number(standMean[0],'f',3) + "</td></tr>");
    html.append("<tr><b><td><b>STD</b></td><td><ALIGN ='right'>" + QString().number(standXStd[0],'f',3) + "</td><td>" +
                QString().number(standyStd[0],'f',3) + "</td><td>" + QString().number(standStd[0],'f',3) + "<sup>o</sup></td></tr>");
    html.append("</table>");

    int cnt = 0;
    QString imagesHtml = "<table  style='ds margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px;'"
                 " 'width='100%' cellspacing='2' cellpadding='0'>";

    QPolygonF mirrorAstigAtEachRotation;
    for (int i = 0; i < list.size(); ++i){
        if (cnt++ == 0)
            imagesHtml.append("<tr>");
        // make plot of stand astig
        QwtPlotCurve *curve = new QwtPlotCurve(QString().number(list[i]->angle));
        curves << curve;
        QPolygonF points1;
        points1 << QPointF(0.,0.);
        points1 << QPointF(standxastig.at<double>(i,0), standyastig.at<double>(i,0));
        QColor color(Qt::GlobalColor( 6 + i%13 ) );
        QPen pen(color);
        curve->setPen(pen);
        curve->setSamples(points1);
        curve->attach(pl1);
        QwtPlotMarker *m = new QwtPlotMarker();
        m->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, color,color, QSize(5,5)));
        mirrorAstigAtEachRotation << QPointF(m_wavefronts[inputs[i]]->InputZerns[4],m_wavefronts[inputs[i]]->InputZerns[5]);
        m->setYValue(m_wavefronts[inputs[i]]->InputZerns[5]);
        m->setXValue(m_wavefronts[inputs[i]]->InputZerns[4]);
        m->attach(pl1);

        // make contour plots of astig zernike terms of stand
        ContourPlot *cp = new ContourPlot();
        cp->m_zRangeMode = "Min/Max";
        wavefront * wf = new wavefront(*m_wavefronts[inputs[i]]);


        wf->data = wf->workData = standwfs[i];
        cv::resize(m_wavefronts[inputs[i]]->workMask, wf->mask, cv::Size(wf->data.cols, wf->data.rows));
        wf->workMask = wf->mask;

        cv::minMaxIdx(wf->data,&smin, &smax);

        cv::Scalar mean,std;
        cv::meanStdDev(wf->data,mean,std);
        wf->std = std[0];
        wf->useSANull = false;
        wf->min = smin;
        wf->max =  smax;
        wf->name = QString().sprintf("%06.2lf",list[i]->angle);

        cp->setSurface(wf);
        cp->resize(Width,Height);
        cp->replot();
        contour.fill( QColor( Qt::white ).rgb() );
        renderer.render( cp, &painter, QRect(0,0,Width,Height) );
        QString imageName = QString().sprintf("mydata://zern%s.png",wf->name.toStdString().c_str());
        imageName.replace("-","CCW");
        doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour));
        results.res.append (imageName);
        imagesHtml.append("<td><p> <img src='" +imageName + "' /></p></td>");
        if (cnt == 2){
            cnt = 0;
            imagesHtml.append("</tr>");
        }
    }

    imagesHtml.append("</table>");
    //display average of all stand zernwavefronts
    wavefront * wf2 = new wavefront(*m_wavefronts[inputs[0]]);
    wf2->data = wf2->workData = standavgZernMat ;
    cv::resize(m_wavefronts[inputs[0]]->mask,wf2->mask, cv::Size(wf2->data.cols, wf2->data.rows));
    wf2->workMask = wf2->mask;
    cv::Scalar mean,std;
    cv::meanStdDev(wf2->data,mean,std);
    wf2->std = std[0];
    double mmin;
    double mmax;

    minMaxIdx(wf2->data, &mmin,&mmax);
    wf2->min = mmin;
    wf2->max = mmax;
    //wf2->workMask = m_wavefronts[0]->workMask.clone();
    wf2->name = QString("Average Stand zernike based");
    ContourPlot *cp1 = new ContourPlot();
    //cp1->m_zRangeMode = "Min/Max";
    cp1->setSurface(wf2);
    cp1->resize(Width, Height);
    cp1->replot();
    QImage contour2(Width, Height, QImage::Format_ARGB32 );
    contour2.fill( QColor( Qt::white ).rgb() );
    QPainter painter2( &contour2 );

    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend, false);
    renderer.render( cp1, &painter2, QRect(0,0,Width,Height) );
    QString imageName = "mydata://StandCotourZerns.png";
    doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour2));

    wf2->data = wf2->workData = standavg;
    wf2->useSANull = false;
    wf2->name = QString("Average Stand effects.");
    cp1->setSurface(wf2);
    cp1->resize(Width,Height);
    cp1->replot();
    contour2.fill( QColor( Qt::white ).rgb() );
    renderer.render( cp1, &painter2, QRect(0,0,Width,Height) );
    imageName = QString("mydata://StandCotourMat.png");
    doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour2));

    pl1->setAxisScale(QwtPlot::xBottom,min(-.1,xmin),max(.1,xmax));
    pl1->setAxisScale(QwtPlot::yLeft, min(-.1, ymin), max(.1,ymax));
    pl1->insertLegend( new QwtLegend() , QwtPlot::TopLegend);
    pl1->setAxisTitle( QwtPlot::yLeft, "Y astig" );
    pl1->setAxisTitle(QwtPlot::xBottom, "X astig");
    //  ...a vertical line at 0...
    QwtPlotMarker *muY = new QwtPlotMarker();
    muY->setLineStyle( QwtPlotMarker::VLine );
    muY->setLinePen( Qt::black, 0, Qt::DashDotLine );
    muY->setXValue( 0.);
    muY->attach( pl1);
    //  ...a horizontal line at 0...
    QwtPlotMarker *mux = new QwtPlotMarker();
    mux->setLineStyle( QwtPlotMarker::HLine );
    mux->setLinePen( Qt::black, 0, Qt::DashDotLine );

    mux->setYValue( 0.);
    mux->attach( pl1);
    // plot the mean
    QwtPlotMarker *meanMark = new QwtPlotMarker();
    meanMark->setSymbol(new QwtSymbol(QwtSymbol::Star1,QColor(255,255,255), QColor(0,0,0), QSize(30,30)));
    meanMark->setLabel(QString("Mean") );
    meanMark->setYValue(standYMean[0]);
    meanMark->setXValue(standXMean[0]);
    meanMark->setLabelAlignment( Qt::AlignTop );
    meanMark->attach(pl1);
    double mirrorXastig = 0;
    double mirrorYastig = 0;
    int count = mirrorAstigAtEachRotation.size();
    for (int i = 0; i < count; ++i){
        mirrorXastig += mirrorAstigAtEachRotation[i].rx();
        mirrorYastig += mirrorAstigAtEachRotation[i].ry();
    }
    mirrorXastig /= count;
    mirrorYastig /= count;
    double avgAstigRradius = 0;
    for (int i = 0; i < count; ++i){
        double x = mirrorAstigAtEachRotation[i].rx();
        double y = mirrorAstigAtEachRotation[i].ry();
        x -= mirrorXastig;
        y -= mirrorYastig;
        double rad = sqrt (x * x + y * y);
        avgAstigRradius += rad;
    }
    avgAstigRradius/= count;

    QwtPlotMarker *mirrorMeanMark = new QwtPlotMarker();
    mirrorMeanMark->setValue(QPointF(mirrorXastig, mirrorYastig));
    mirrorMeanMark->setSymbol(new QwtSymbol(QwtSymbol::Triangle,QColor(0,255,0,40), QColor(0,0,0), QSize(30,30)));
    mirrorMeanMark->attach(pl1);

    QPolygonF stdCircle;
    double SE = standStd[0]/sqrt(standastig.rows);
    for (double rho = 0; rho <= 2 * M_PI; rho += M_PI/32.){
        stdCircle << QPointF(standXMean[0]+SE * cos(rho),standYMean[0] + SE * sin(rho));
    }
    QwtPlotCurve *curveStandStd = new QwtPlotCurve("Standard Error");
    curveStandStd->setPen(Qt::darkYellow,3,Qt::DotLine );
    curveStandStd->setSamples(stdCircle);
    curveStandStd->attach(pl1);
    for (int i = 0; i < count; ++i){
        double x = mirrorXastig - mirrorAstigAtEachRotation[i].rx();

        double y = mirrorYastig - mirrorAstigAtEachRotation[i].ry();
        double rad = sqrt(x * x + y * y);
        stdCircle.clear();
        for (double rho = 0; rho <= 2 * M_PI; rho += M_PI/32.){
            stdCircle << QPointF(mirrorXastig+rad * cos(rho), mirrorYastig + rad * sin(rho));
        }
        curveStandStd = new QwtPlotCurve();
        QColor color(Qt::GlobalColor( 6 + i%13 ) );
        QPen pen(color,2);
        curveStandStd->setPen(pen);
        curveStandStd->setSamples(stdCircle);
        curveStandStd->attach(pl1);
    }

    pl1->resize(300,300);
    pl1->replot();

    contour.fill( QColor( Qt::white ).rgb() );
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend, false);
    renderer.render( pl1, &painter, QRect(0,0,Width,Height) );
    imageName = QString("mydata://plot.png");
    doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour));
    results.res.append (imageName);
    html.append("<p> <img src='" + imageName + "' /></p>");
    html.append("<p font-size:12pt> The plot above shows the astig of each input file plotted as colored squares and large circle."
                "The center of these circles is a green triangle. The green triangle is the mean value of all the unrotated input files."""
                " In a perfect world each circle would have the same radius and overlap. "
                " If any circles are far different from the others look for a problem at that rotation angle.</p>"
               "<p font-size:12pt>It shows the stand only astig ploted as lines. The line ends at the atig value. "
               "The length of the line represents the magnitude of the astig.<br>"
               "The mean value of test stand only astig is plotted as a large star. It and the green triangle should overlap.<br>"
                "There is a circle around the mean value that is the standard error of the mean. Its diameter represents the "
                "variabilty of the mean.</p>"
                "<p font-size:12pt>If the variation for the astig values is small then "
                "The stand removal was good.  Idealy the STD (standard deviation) should be"
                " less than .1 which means less than .1 wave pv on the surface of the mirror</p><br>"


                "<table><tr><td><img src='mydata://StandCotourZerns.png' /></td><td><img src='mydata://StandCotourMat.png' /></td></tr></table>"
                "<p font-size:12pt>The contours above are the average system induced forces derived from the average of all rotations.<br>"
                "The left contour is based on the zernike values and the contour on the right is based on the wavefront.</p>"
                "<p font-size:12pt>The contour plots below are of what is beleived to be test stand only induced errors at each rotation. "
                "Check that they are similar at each rotation.  If not then maybe "
                "stand (system) induced error is not same at each rotation then the stand removal is not reliable. "
                "However it is unlikely that they will all look exactly the same.</p>");


    html.append(imagesHtml);
    html.append("</body></html>");
    editor->setHtml(html);
    return results;
}

void SurfaceManager::computeStandAstig(define_input *wizPage, QList<rotationDef *> list){
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // check for pairs
    QVector<rotationDef*> lookat = list.toVector();
    while (lookat.size()){
        for (int i = 0; i < lookat.size(); ++i){
            double angle1 = lookat[i]->angle;
            double found = false;
            for (int j = i+1; j < lookat.size(); ++j){
                if (lookat[j]->angle == angle1 -90 || lookat[j]->angle == angle1 + 90)
                {
                    found = true;
                    lookat.remove(j);
                    lookat.remove(i);
                    found = true;
                    break;
                }
            }

            if (!found){
                QMessageBox::warning(0, tr("Error"),
                                     QString().sprintf("No 90 deg pair for %s and angle %6.2lf",
                                                       lookat[i]->fname.toStdString().c_str(),
                                                       lookat[i]->angle));
                lookat.remove(i);
            }
            break;
        }
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setColorMode( QPrinter::Color );
    printer.setFullPage( true );
    printer.setOutputFileName( "stand.pdf" );
    printer.setOutputFormat( QPrinter::PdfFormat );
    printer.setResolution(85);
    printer.setPaperSize(QPrinter::A4);


    QTextEdit *editor = new QTextEdit;
    editor->resize(printer.pageRect().size());
    const int contourWidth = 2 * 340/3;
    const int contourHeight = 2 * 360/3;
    QImage contour = QImage(contourWidth ,contourHeight, QImage::Format_ARGB32 );


    QwtPlotRenderer renderer;
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasFrame );
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend);
    ContourPlot *plot =new ContourPlot(0,0,true);//m_contourPlot;
    plot->m_minimal = true;
    QList<int> unrotatedNdxs;
    QList<int> rotatedNdxs;


    QString html = ("<html><head/><body><h1><center>Test Stand Astig Removal</center></h1>"
                    "<h2><center>" + AstigReportTitle);
           html.append("    <font color='grey'>" + QDate::currentDate().toString() +
                       " " +QTime::currentTime().toString()+"</font></center><h2>");
           html.append("<h3>Step 1. Counter rotate input files results:</h3>"
                       "Check that all the counter rotated images appear to be oriented the same way."
                       "If the stand astig is equal to or larger than the mirror astig they may not appear to be oriented the same way."
                       "But continue anyway and look for other features on the surface that rotated if possible."
            "<table border='1'style='ds margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px;'"
            " 'width='100%' cellspacing='2' cellpadding='0'>");
    html.append("<tr><td><p align='center'><b>Unrotated inputs</b></p></td>");
    html.append("<td><p align='center'><b> Counter Rotated </b></p></td></tr>");


    QTextDocument *doc = editor->document();
    QList<QString> doc1Res;
    doc->setPageSize(printer.pageRect().size()); // This is necessary if you want to hide the page number
    QList<int> rotated;
    QList<int> inputs;
    editor->append("<tr>");
    int startingNdx = m_wavefronts.size() -1;

    for (int i = 0; i < list.size(); ++i){
        contour.fill( QColor( Qt::white ).rgb() );
        QPainter painter( &contour );
        int ndx = m_wavefronts.size();
        inputs.append(ndx);
        // get input file

        m_surface_finished = false;
        QStringList loadList;
        loadList << list[i]->fname;
        emit load(loadList, this);
        while (!m_surface_finished){qApp->processEvents();} // wait for wavefront to be computed

        wavefront * wf = m_wavefronts[m_currentNdx];
        unrotatedNdxs.append(m_currentNdx);
        plot->setSurface(wf);
        plot->replot();
        renderer.render( plot, &painter, QRect(0,0,contourWidth,contourHeight) );

        QString imageName = QString().sprintf("mydata://%s.png",list[i]->fname.toStdString().c_str());
        QString angle = QString().sprintf("%6.2lf Deg",list[i]->angle);
        doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour));
        doc1Res.append(imageName);
        html.append("<tr><td><p align='center'> <img src='" +imageName + "' /><br><b>" + angle + "</b></p></td>");


        // counter rotate it
        wizPage->runpb->setText(QString("Counter Rotating " + list[i]->fname));
        QList<int> l;
        l.append(ndx);
        ndx = m_wavefronts.size();

        m_surface_finished = false;
        rotateThese(list[i]->angle,l);
        rotated.append(ndx);
        wf = m_wavefronts[ndx];
        while (!m_surface_finished){qApp->processEvents();}
        plot->setSurface(wf);
        plot->replot();

        contour.fill( QColor( Qt::white ).rgb() );
        renderer.render( plot, &painter, QRect(0,0,contourWidth,contourHeight) );
        angle = QString().sprintf("%6.2lf Deg",list[i]->angle);
        imageName = QString().sprintf("mydata://CR%s%s.png",list[i]->fname.toStdString().c_str(),angle.toStdString().c_str());


        doc->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour));
        doc1Res.append(imageName);
        html.append("<td><p align='center'> <img src='" +imageName + "' /><br><b>Counter " + angle + "</b></p></td>");
        html.append("</tr>");
    }
    html.append("</table></body></html>");

    editor->setHtml(html);
    editor->setDocument(doc);

    //editor->show();

    // Now average all the rotated ones.
    QList<wavefront *> wlist;
    for (int i = 0; i < rotated.size(); ++i){
        wlist << m_wavefronts[rotated[i]];
    }

    int avgNdx = m_wavefronts.size();  // the index of the average

    m_surface_finished = false;

    average(wlist);
    while (!m_surface_finished){qApp->processEvents();}
    emit(nameChanged(m_wavefronts[m_currentNdx]->name, QString("AverageStandRemoved")));
    QTextEdit *page2 = new QTextEdit;
    page2->resize(600,800);
    QTextDocument *doc2 = page2->document();
    QList<QString> doc2Res;
    html = ("<html><head/><body><h1>Test Stand Astig Removal</h1>"
            "<h3>Step 2. Averaged surface with stand induced terms removed:</h3>");


    plot->setSurface(m_wavefronts[m_currentNdx]);
    contour = QImage(450,450, QImage::Format_ARGB32 );
    contour.fill( QColor( Qt::white ).rgb() );
    QPainter painter( &contour );

    renderer.render( plot, &painter, QRect(0,0,450,450) );

    QString imageName = "mydata://AvgAstigremoved.png";
    doc2->addResource(QTextDocument::ImageResource,  QUrl(imageName), QVariant(contour));
    doc2Res.append(imageName);
    html.append("<p> <img src='" +imageName + "' /></p>");
    html.append("</body></html>");
    page2->setDocument(doc2);
    page2->setHtml(html);
    //page2->show();

    // calculate stand astig for each input
    // for each input rotate the average by the input angle and subtract it from the input
    // plot the astig of each of the inputs which will be the stand only astig.

    wizPage->runpb->setText("computing averages");
    textres page3res = Phase2(list, inputs, avgNdx);
    QTabWidget *tabw = new QTabWidget();
    tabw->setTabShape(QTabWidget::Triangular);
    tabw->addTab(editor, "Page 1 input analysis");
    tabw->addTab(page2, "Page 2 Stand removed.");
    tabw->addTab(page3res.Edit, "Page 3 stand analysis");
    tabw->resize(800,600);
    tabw->show();

    // build pdf doc from the three textedits
    QTextDocument pdfDoc;
    foreach( QString res, page3res.res){
        pdfDoc.addResource(QTextDocument::ImageResource, res, page3res.Edit->document()->resource(QTextDocument::ImageResource,res));
    }
    pdfDoc.addResource(QTextDocument::ImageResource, QString("mydata://StandCotourZerns.png"),
                       page3res.Edit->document()->resource(QTextDocument::ImageResource,QString("mydata://StandCotourZerns.png") ));
    pdfDoc.addResource(QTextDocument::ImageResource, QString("mydata://StandCotourMat.png"),
                       page3res.Edit->document()->resource(QTextDocument::ImageResource,QString("mydata://StandCotourMat.png") ));
    foreach(QString res, doc1Res){
        pdfDoc.addResource(QTextDocument::ImageResource, res, editor->document()->resource(QTextDocument::ImageResource,res));
    }
    foreach(QString res, doc2Res){
        pdfDoc.addResource(QTextDocument::ImageResource, res, page2->document()->resource(QTextDocument::ImageResource,res));
    }

     printer.setOutputFileName(AstigReportPdfName);
     QTextCursor cursor(&pdfDoc);
     cursor.insertHtml(editor->toHtml());
     QTextBlockFormat blockFormat;
     blockFormat.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
     cursor.insertBlock(blockFormat);
     cursor.insertHtml(page2->toHtml());
     cursor.insertBlock(blockFormat);
     cursor.insertHtml(page3res.Edit->toHtml());
     pdfDoc.print(&printer);

     //Delete all work wavefronts except the overall average with stand removed.
     QList<int> deleteThese;
     for (int i = m_wavefronts.size() -1; i > startingNdx; --i){
         if (i != avgNdx)
            deleteThese.append(i);
     }

     deleteWaveFronts(deleteThese);

    wizPage->runpb->setText("Compute");
    wizPage->runpb->setEnabled(true);
    QApplication::restoreOverrideCursor();
}

void SurfaceManager::computeTestStandAstig(){
    standAstigWizard *wiz = new standAstigWizard(this);
    wiz->show();
}

void SurfaceManager::saveAllContours(){
    QSettings settings;
    QString lastPath = settings.value("projectPath","").toString();
    const QList<QByteArray> imageFormats = QImageWriter::supportedImageFormats();
    QStringList filter;
    QString imageFilter( tr( "Images" ) );
    imageFilter += " (";
    for ( int i = 0; i < imageFormats.size(); i++ )
    {
        if ( i > 0 )
            imageFilter += " ";
        imageFilter += "*.";
        imageFilter += imageFormats[i];
    }
    imageFilter += ")";

    filter += imageFilter;

    QString fName = QFileDialog::getSaveFileName(0,
    tr("Save stats plot"), lastPath + "//allConturs.jpg",filter.join( ";;" ));
    if (fName.isEmpty())
        return;
    m_allContours.save( fName );
}
void SurfaceManager::showAll3D(GLWidget *gl)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int width = 500;
    int height = 500;

    int rows =  ceil((double)m_wavefronts.size()/4.);
    int columns = min(m_wavefronts.size(),int(ceil((double)m_wavefronts.size()/rows)));
    const QSizeF size(columns * (width + 10), rows * (height + 10));
    const QRect imageRect = QRect(0,0,size.width(),size.height());
    m_allContours = QImage( imageRect.size(), QImage::Format_ARGB32 );
    m_allContours.fill( QColor( Qt::white ).rgb() );
    QPainter painter(&m_allContours);
    QFont serifFont("Times", 18, QFont::Bold);
    for (int i = 0; i < m_wavefronts.size(); ++i)
    {
        wavefront * wf = m_wavefronts[i];
        gl->setSurface(wf);
        QImage glImage = gl->grabFrameBuffer();
        QPainter p2(&glImage);
        p2.setFont(serifFont);
        p2.setPen(QPen(QColor(Qt::white)));
        QStringList l = wf->name.split("/");
        p2.drawText(10,30,l[l.size()-1] + QString().sprintf("%6.3lf RMS",wf->std));

        int y_offset =  height * (i/columns) + 40;
        int x_offset = width * (i%columns) + 20;
        painter.drawImage(x_offset,y_offset, glImage.scaled(width, height,Qt::KeepAspectRatio));
    }

    //image.save( "tmp.png" );
    QWidget *w = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout;

    QScrollArea *scrollArea = new QScrollArea;

    QLabel *l = new QLabel();
    l->setPixmap(QPixmap::fromImage(m_allContours));
    l->setScaledContents( true );

    l->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Ignored );

    scrollArea->setWidget(l);
    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setAutoFillBackground(true);
    QPushButton *savePb = new QPushButton("Save as Image",w);

    connect(savePb, SIGNAL(pressed()), this, SLOT(saveAllContours()));
    layout->addWidget(savePb,0,Qt::AlignHCenter);
    layout->addWidget(scrollArea);
    w->setLayout(layout);
    w->setWindowTitle("3D height map of All WaveFronts.");
    QRect rec = QApplication::desktop()->screenGeometry();
    height = 2 * rec.height()/3;
    width = rec.width();
    w->resize(width,height);
    w->show();
    QApplication::restoreOverrideCursor();
}

void SurfaceManager::showAllContours(){
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ContourPlot *plot =new ContourPlot(0,0);//m_contourPlot;
    //plot->m_minimal = true;
    int width = 420;
    int height = 350;

    int rows =  ceil((double)m_wavefronts.size()/4.);
    int columns = min(m_wavefronts.size(),int(ceil((double)m_wavefronts.size()/rows)));
    const QSizeF size(columns * (width + 10), rows * (height + 10));
    const QRect imageRect = QRect(0,0,size.width(),size.height());
    m_allContours = QImage( imageRect.size(), QImage::Format_ARGB32 );
    m_allContours.fill( QColor( Qt::white ).rgb() );
    QPainter painter( &m_allContours );
    QwtPlotRenderer renderer;
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasBackground );
    renderer.setDiscardFlag( QwtPlotRenderer::DiscardCanvasFrame );
    renderer.setDiscardFlag(QwtPlotRenderer::DiscardLegend,false);
    renderer.setLayoutFlag( QwtPlotRenderer::FrameWithScales,false );
    for (int i = 0; i < m_wavefronts.size(); ++i)
    {
        wavefront * wf = m_wavefronts[i];
        plot->setSurface(wf);
        plot->replot();
        int y_offset =  height * (i/columns) + 10;
        int x_offset = width * (i%columns) + 10;
        const QRectF topRect( x_offset, y_offset, width, height );
        renderer.render( plot, &painter, topRect );
    }
    painter.end();

    //image.save( "tmp.png" );
    QWidget *w = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout;

    QScrollArea *scrollArea = new QScrollArea;

    QLabel *l = new QLabel();
    l->setPixmap(QPixmap::fromImage(m_allContours));
    l->setScaledContents( true );

    l->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Ignored );

    scrollArea->setWidget(l);
    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setAutoFillBackground(true);
    QPushButton *savePb = new QPushButton("Save as Image",w);

    connect(savePb, SIGNAL(pressed()), this, SLOT(saveAllContours()));
    layout->addWidget(savePb,0,Qt::AlignHCenter);
    layout->addWidget(scrollArea);
    w->setLayout(layout);
    w->setWindowTitle("Contours of all WaveFronts.");
    QRect rec = QApplication::desktop()->screenGeometry();
    height = 2 * rec.height()/3;
    width = rec.width();
    w->resize(width,height);
    w->show();
    QApplication::restoreOverrideCursor();
}
void SurfaceManager::report(){
    QString fileName = QFileDialog::getSaveFileName((QWidget* )0, "Save PDF report", QString(mirrorDlg::get_Instance()->getProjectPath() +
                                                                                        "/Report.pdf"), "*.pdf");
    if (fileName.isEmpty())
        return;
    if (QFileInfo(fileName).suffix().isEmpty()) { fileName.append(".pdf"); }

    QPrinter printer(QPrinter::HighResolution);
    printer.setColorMode( QPrinter::Color );
    printer.setFullPage( true );
    printer.setOutputFileName( fileName );
    printer.setOutputFormat( QPrinter::PdfFormat );
    printer.setResolution(85);
    printer.setPaperSize(QPrinter::A4);

    QTextEdit *editor = new QTextEdit;
    editor->resize(printer.pageRect().size());

    mirrorDlg *md = mirrorDlg::get_Instance();
    wavefront *wf = m_wavefronts[m_currentNdx];

    metricsDisplay *metrics = metricsDisplay::get_instance();



    QString title("<html><body><table width = '100%'><tr><td></td><td><h1><center>Interferometry Report for " +
                  md->m_name + "</center></td><td>"
                  + QDate::currentDate().toString() +
                  " " +QTime::currentTime().toString()+"</td></tr></table>");

    QString Diameter = (md->isEllipse()) ? " Horizontal Axis: " : " Diameter: " +QString().number(md->diameter,'f',1) ;
    QString ROC = (md->isEllipse()) ? "Vertical Axis: " + QString().number(md->m_verticalAxis) : "ROC: " +  QString().number(md->roc,'f',1);
    QString FNumber = (md->isEllipse()) ? "" : "Fnumber: " + QString().number(md->FNumber,'f',1);
    QString BFC = (md->isEllipse()) ? " Flat" : metrics->mCC->text();
    QString html = "<p style=\'font-size:15px'>"
            "<table border='1' width = '100%'><tr><td>" + Diameter + " mm</td><td>" + ROC + " mm</td>"
            "<td>" +FNumber+ "</td></tr>"
            "<tr><td> RMS: " + QString().number(wf->std,'f',3) + " waves at 550nm</td><td>Strehl: " + metrics->mStrehl->text() +
            "</td><td>" + BFC + "</td></tr>"
            "<tr><td>" + ((md->isEllipse()) ? "":"Desired Conic: " + QString::number(md->cc)) + "</td><td>" +
            ((md->doNull) ? QString().sprintf("SANull: %6.4lf",md->z8) : "No software Null") + "</td>"
            "<td>Waves per fringe: " + QString::number(md->fringeSpacing) + "<br>Test Wave length: "+ QString::number(md->lambda) + "nm</td></tr>"
            "</table></p>";

    // zerenike values
    QString zerns("<p>This is a flat so no zernike values are computed.</p>");
    if (!md->isEllipse()){
        zerns = "<p>Zernike Values :<br><table width='100%'><tr><td><table  border='1' width='40%'>";
        zerns.append("<tr><td>Term</td><td><table width = '100%'><tr><td>Wyant</td><td>RMS</td></tr></table></tr>");
        int half = Z_TERMS/2 +2;
        for (int i = 3; i < half; ++i){
            double val = wf->InputZerns[i];
            bool enabled = zernEnables[i];
            if ( i == 3 && m_surfaceTools->m_useDefocus){
                val = m_surfaceTools->m_defocus;
                enabled = true;
            }
            if ( i == 8 && md->doNull){
                val += md->z8;
            }

            zerns.append("<tr><td>" + QString(zernsNames[i]) + "</td><td><table width = '100%'><tr><td>" + QString().sprintf("%6.3lf </td><td>%6.3lf</td></tr></table>",
                                                                                                                             val, computeRMS(i,val)) + "</td><td>" +
                         QString((enabled) ? QString("") : QString("Disabled")) + "</td></tr>");
        }


        zerns.append("</table></td><td><table border='1' width = '50%'>");
        zerns.append("<tr><td>Term</td><td><table width = '100%'><tr><td>Wyant</td><td>RMS</td></tr></table></tr>");

        for (int i = half; i < Z_TERMS; ++i){
            double val = wf->InputZerns[i];
            zerns.append("<tr><td>" + QString(zernsNames[i]) + "</td><td><table width = '100%'><tr><td>" + QString().sprintf("%6.3lf</td><td>%6.3lf</td></tr></table>"
                                                                                                                             ,val, computeRMS(i,val)) + "</td><td>" +
                         QString((zernEnables[i]) ? QString("") : QString("Disabled")) + "</td></tr>");
        }
        zerns.append("</table></td></tr></table></p>");
        //qDebug() << zerns;
    }
    QString tail = "</body></html>";

    QImage image = QImage(600,500, QImage::Format_ARGB32 );

    QPainter painter(&image);
    ContourPlot *plot =new ContourPlot(0,0,false);//m_contourPlot;
    plot->setSurface(wf);
    QwtPlotRenderer renderer;
    renderer.render(plot,&painter,QRect(0,0,600,500));
    QTextDocument *doc = editor->document();
    QString contour("mydata://contour.png");
    doc->addResource(QTextDocument::ImageResource,  QUrl(contour), QVariant(image));
    QString contourHtml = "<p> <img src='" +contour + "'</p>";
    QString threeD("threeD");
    image.fill( QColor( Qt::white ).rgb() );
    QImage ddd = m_oglPlot->renderPixmap(600,400).toImage();

    doc->addResource(QTextDocument::ImageResource,  QUrl(threeD), QVariant(ddd));
    contourHtml.append("<p> <img src='" +threeD + "'</p>");
    QImage i2 = QImage(((MainWindow*)(parent()))->m_profilePlot->size(),QImage::Format_ARGB32);
    QPainter p2(&i2);
    image.fill( QColor( Qt::white ).rgb());
    //renderer.render(m_profilePlot->m_plot, &painter,QRect(0,0,600,300));
    ((MainWindow*)(parent()))->m_profilePlot->render(&p2);
    QImage i2Scaled = i2.scaled(printer.pageRect().size().width()-50,800,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    QString profile("mydata://profile.png");
    doc->addResource(QTextDocument::ImageResource,  QUrl(profile), QVariant(i2Scaled));
    contourHtml.append("<p> <img src='" +profile + "'</p>");
    if (!md->isEllipse()){
        SimulationsView *sv = SimulationsView::getInstance(0);
        if (sv->needs_drawing){
            sv->on_MakePB_clicked();
        }
        QImage svImage = QImage(sv->size(),QImage::Format_ARGB32 );
        QPainter p3(&svImage);
        sv->render(&p3);
        QImage svImageScaled = svImage.scaled(printer.pageRect().size().width()-50,800,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        QString svpng("mydata://sv.png");
        doc->addResource(QTextDocument::ImageResource,  QUrl(svpng), QVariant(svImageScaled));
        contourHtml.append("<p> <img src='" +svpng + "'</p>");
    }
    // add igram to bottom of report
    QImage igram = ((MainWindow*)(parent()))->m_igramArea->igramDisplay;
    if (igram.width() > 0){
        QString sigram("mydata://igram.png");
        doc->addResource(QTextDocument::ImageResource,  QUrl(sigram),
                         QVariant(igram.scaled(printer.pageRect().size().width()-50,800,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
        contourHtml.append("<p> <img src='" +sigram + "'</p>Typical Interferogram");
    }
    editor->setHtml(title + html +zerns + contourHtml+ tail);
    editor->print(&printer);
    editor->show();
}


