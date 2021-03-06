/*----------------------------------------------------------------------------*/
/* Copyright (c) 2017-2020 FIRST. All Rights Reserved.                        */
/* Open Source Software - may be modified and shared by FRC teams. The code   */
/* must be accompanied by the FIRST BSD license file in the root directory of */
/* the project.                                                               */
/*----------------------------------------------------------------------------*/

#include <frc/DriverStation.h>
#include <frc/Encoder.h>
#include <frc/GenericHID.h>
#include <frc/PWMVictorSPX.h>
#include <frc/StateSpaceUtil.h>
#include <frc/TimedRobot.h>
#include <frc/XboxController.h>
#include <frc/controller/LinearPlantInversionFeedforward.h>
#include <frc/controller/LinearQuadraticRegulator.h>
#include <frc/drive/DifferentialDrive.h>
#include <frc/estimator/KalmanFilter.h>
#include <frc/system/LinearSystemLoop.h>
#include <frc/system/plant/DCMotor.h>
#include <frc/system/plant/LinearSystemId.h>
#include <wpi/math>

/**
 * This is a sample program to demonstrate how to use a state-space controller
 * to control a flywheel.
 */
class Robot : public frc::TimedRobot {
  static constexpr int kMotorPort = 0;
  static constexpr int kEncoderAChannel = 0;
  static constexpr int kEncoderBChannel = 1;
  static constexpr int kJoystickPort = 0;
  static constexpr units::radians_per_second_t kSpinup = 500_rpm;

  // Volts per (radian per second)
  static constexpr double kFlywheelKv = 0.023;

  // Volts per (radian per second squared)
  static constexpr double kFlywheelKa = 0.001;

  // The plant holds a state-space model of our flywheel. This system has the
  // following properties:
  //
  // States: [velocity], in radians per second.
  // Inputs (what we can "put in"): [voltage], in volts.
  // Outputs (what we can measure): [velocity], in radians per second.
  //
  // The Kv and Ka constants are found using the FRC Characterization toolsuite.
  frc::LinearSystem<1, 1, 1> m_flywheelPlant =
      frc::LinearSystemId::IdentifyVelocitySystem(kFlywheelKv, kFlywheelKa);

  // The observer fuses our encoder data and voltage inputs to reject noise.
  frc::KalmanFilter<1, 1, 1> m_observer{
      m_flywheelPlant,
      {3.0},   // How accurate we think our model is
      {0.01},  // How accurate we think our encoder data is
      20_ms};

  // A LQR uses feedback to create voltage commands.
  frc::LinearQuadraticRegulator<1, 1> m_controller{
      m_flywheelPlant,
      // qelms. Velocity error tolerance, in radians per second. Decrease this
      // to more heavily penalize state excursion, or make the controller behave
      // more aggressively.
      {8.0},
      // relms. Control effort (voltage) tolerance. Decrease this to more
      // heavily penalize control effort, or make the controller less
      // aggressive. 12 is a good starting point because that is the
      // (approximate) maximum voltage of a battery.
      {12.0},
      // Nominal time between loops. 20ms for TimedRobot, but can be lower if
      // using notifiers.
      20_ms};

  // Plant-inversion feedforward calculates the voltages necessary to reach our
  // reference.
  frc::LinearPlantInversionFeedforward<1, 1> m_feedforward{m_flywheelPlant,
                                                           20_ms};

  // The state-space loop combines a controller, observer, feedforward and plant
  // for easy control.
  frc::LinearSystemLoop<1, 1, 1> m_loop{m_flywheelPlant, m_controller,
                                        m_feedforward, m_observer, 12_V};

  // An encoder set up to measure flywheel velocity in radians per second.
  frc::Encoder m_encoder{kEncoderAChannel, kEncoderBChannel};

  frc::PWMVictorSPX m_motor{kMotorPort};
  frc::XboxController m_joystick{kJoystickPort};

 public:
  void RobotInit() {
    // We go 2 pi radians per 4096 clicks.
    m_encoder.SetDistancePerPulse(2.0 * wpi::math::pi / 4096.0);
  }

  void TeleopInit() {
    m_loop.Reset(frc::MakeMatrix<1, 1>(m_encoder.GetRate()));
  }

  void TeleopPeriodic() {
    // Sets the target speed of our flywheel. This is similar to setting the
    // setpoint of a PID controller.
    if (m_joystick.GetBumper(frc::GenericHID::kRightHand)) {
      // We pressed the bumper, so let's set our next reference
      m_loop.SetNextR(frc::MakeMatrix<1, 1>(kSpinup.to<double>()));
    } else {
      // We released the bumper, so let's spin down
      m_loop.SetNextR(frc::MakeMatrix<1, 1>(0.0));
    }

    // Correct our Kalman filter's state vector estimate with encoder data.
    m_loop.Correct(frc::MakeMatrix<1, 1>(m_encoder.GetRate()));

    // Update our LQR to generate new voltage commands and use the voltages to
    // predict the next state with out Kalman filter.
    m_loop.Predict(20_ms);

    // Send the new calculated voltage to the motors.
    // voltage = duty cycle * battery voltage, so
    // duty cycle = voltage / battery voltage
    m_motor.SetVoltage(units::volt_t(m_loop.U(0)));
  }
};

#ifndef RUNNING_FRC_TESTS
int main() { return frc::StartRobot<Robot>(); }
#endif
