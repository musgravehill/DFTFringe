#ifndef FOUCAULTVIEW_H
#define FOUCAULTVIEW_H

#include <QWidget>
#include "surfacemanager.h"
#include <QTimer>
namespace Ui {
class foucaultView;
}

class foucaultView : public QWidget
{
    Q_OBJECT

public:
    static foucaultView *get_Instance(SurfaceManager *sm = 0);
    static foucaultView *m_instance;
    explicit foucaultView(QWidget *parent = 0, SurfaceManager *sm = 0);
    ~foucaultView();
    void setSurface(wavefront * wf);
    bool needsDrawing;
public slots:
    void on_makePb_clicked();
private slots:

    void on_gammaSb_valueChanged(double arg1);

    void on_lpiSb_valueChanged(double arg1);

    void on_movingSourceRb_clicked(bool checked);

    void on_radioButton_2_clicked();

    void on_knifeOnLeftCb_clicked();

    void on_lpiSb_editingFinished();

    void on_rocOffsetSb_editingFinished();

    void on_slitWidthSb_editingFinished();

    void on_useMM_clicked(bool checked);

    void on_scanPb_clicked();

    void on_h1x_clicked();

    void on_h2x_clicked();

    void on_h4x_clicked();

    void on_rocOffsetSlider_valueChanged(int value);

    void on_clearCenterCb_clicked();

    void on_autoStepSize_clicked(bool checked);

    void on_rocStepSize_editingFinished();

private:
    Ui::foucaultView *ui;
    SurfaceManager *m_sm;
    QTimer m_guiTimer;
    cv::Mat compute_star_test(int pupil_size, double defocus, double pad, bool use_OPD);
    double getStep();
    int heightMultiply;
    double m_sag;
    wavefront *m_wf;
};

#endif // FOUCAULTVIEW_H
