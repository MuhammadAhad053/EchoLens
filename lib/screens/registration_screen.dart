import 'dart:math';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:http/http.dart' as http;
import 'package:flutter_dotenv/flutter_dotenv.dart';

class RegistrationScreen extends StatefulWidget {
  const RegistrationScreen({super.key});

  @override
  State<RegistrationScreen> createState() => _RegistrationScreenState();
}

class _RegistrationScreenState extends State<RegistrationScreen> {
  final _formKey = GlobalKey<FormState>();
  final _usernameController = TextEditingController();
  final _emailController = TextEditingController();
  final _passwordController = TextEditingController();
  final _otpController = TextEditingController(); 
  
  bool _isLoading = false;
  bool _isOtpSent = false;
  String? _generatedOtp;
  String? _error;

  /// Helper function to send email via EmailJS
  Future<void> _sendEmailJS(String toEmail, String otpCode) async {
    // 1. REPLACE THESE WITH YOUR ACTUAL EMAILJS CREDENTIALS
    final String serviceId = dotenv.env['SERVICE_ID'] ?? '';
    final String templateId = dotenv.env['TEMPLATE_ID'] ?? '';
    final String publicKey = dotenv.env['PUBLIC_KEY'] ?? '';

    final url = Uri.parse('https://api.emailjs.com/api/v1.0/email/send');
    
    try {
      final response = await http.post(
        url,
        headers: {
          'Content-Type': 'application/json',
          'Origin': 'http://localhost',
        },
        body: json.encode({
          'service_id': serviceId,
          'template_id': templateId,
          'user_id': publicKey,
          'template_params': {
            'user_email': toEmail,
            'otp_code': otpCode,
          }
        }),
      );

      if (response.statusCode != 200) {
        throw Exception('EmailJS failed: ${response.body}');
      }
    } catch (e) {
      debugPrint("EmailJS Error: $e");
      rethrow; 
    }
  }

  /// Checks if email exists, then sends code
  Future<void> _sendVerificationCode() async {
    if (!_formKey.currentState!.validate()) return;

    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      // 1. Check if email is already in use
      // ignore: deprecated_member_use
      final list = await FirebaseAuth.instance.fetchSignInMethodsForEmail(_emailController.text.trim());
      
      if (list.isNotEmpty) {
        setState(() {
          _isLoading = false;
          _error = "This email is already in use. Please log in.";
        });
        return;
      }

      // 2. Generate random 6-digit code
      final rng = Random();
      final code = (rng.nextInt(900000) + 100000).toString();

      // 3. Call EmailJS
      await _sendEmailJS(_emailController.text.trim(), code);

      setState(() {
        _generatedOtp = code;
        _isOtpSent = true; 
        _isLoading = false;
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Verification code sent to ${_emailController.text}')),
        );
      }
    } on FirebaseAuthException catch (e) {
      setState(() {
        _isLoading = false;
        _error = e.message;
      });
    } catch (e) {
      setState(() {
        _isLoading = false;
        _error = "Failed to send email. Please check your connection.";
      });
    }
  }

  Future<void> _register() async {
    if (!_formKey.currentState!.validate()) return;

    if (_otpController.text.trim() != _generatedOtp) {
      setState(() => _error = "Invalid verification code.");
      return;
    }

    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      final UserCredential credential = await FirebaseAuth.instance
          .createUserWithEmailAndPassword(
        email: _emailController.text.trim(),
        password: _passwordController.text.trim(),
      );

      final User? user = credential.user;

      if (user != null) {
        await FirebaseFirestore.instance.collection('users').doc(user.uid).set({
          'username': _usernameController.text.trim(),
          'email': _emailController.text.trim(),
          'createdAt': FieldValue.serverTimestamp(),
        });

        if (mounted) {
          Navigator.of(context).pop(); 
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text("Account created successfully!")),
          );
        }
      }
    } on FirebaseAuthException catch (e) {
      setState(() => _error = e.message);
    } catch (e) {
      setState(() => _error = "An unexpected error occurred.");
    } finally {
      if (mounted) setState(() => _isLoading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    const brandColor = Color.fromARGB(255, 212, 160, 24);

    return Scaffold(
      backgroundColor: const Color(0xFF1C1C1C),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        leading: const BackButton(color: Colors.white),
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24.0),
        child: Form(
          key: _formKey,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text("Create Account", 
                style: TextStyle(color: brandColor, fontSize: 32, fontWeight: FontWeight.bold)),
              const SizedBox(height: 8),
              const Text("Join EchoLens to start your research", 
                style: TextStyle(color: Colors.white54, fontSize: 16)),
              const SizedBox(height: 40),

              TextFormField(
                controller: _usernameController,
                style: const TextStyle(color: Colors.white),
                enabled: !_isOtpSent,
                decoration: _inputDecoration("Username", Icons.person_outline, brandColor),
                validator: (val) => val!.isEmpty ? "Enter a username" : null,
              ),
              const SizedBox(height: 20),

              TextFormField(
                controller: _emailController,
                style: const TextStyle(color: Colors.white),
                enabled: !_isOtpSent, 
                decoration: _inputDecoration("Email", Icons.email_outlined, brandColor),
                validator: (val) => val!.contains("@") ? null : "Enter a valid email",
              ),
              const SizedBox(height: 20),

              TextFormField(
                controller: _passwordController,
                obscureText: true,
                style: const TextStyle(color: Colors.white),
                enabled: !_isOtpSent, 
                decoration: _inputDecoration("Password", Icons.lock_outline, brandColor),
                validator: (val) => val!.length < 6 ? "Minimum 6 characters" : null,
              ),
              
              if (_isOtpSent) ...[
                const SizedBox(height: 20),
                TextFormField(
                  controller: _otpController,
                  style: const TextStyle(color: Colors.white, letterSpacing: 2.0, fontWeight: FontWeight.bold),
                  keyboardType: TextInputType.number,
                  maxLength: 6,
                  decoration: _inputDecoration("Enter 6-Digit Code", Icons.verified_user_outlined, brandColor),
                  validator: (val) => val!.length != 6 ? "Enter 6 digits" : null,
                ),
              ],

              if (_error != null) ...[
                const SizedBox(height: 20),
                Text(_error!, style: const TextStyle(color: Colors.redAccent)),
              ],

              const SizedBox(height: 40),
              SizedBox(
                width: double.infinity,
                height: 55,
                child: ElevatedButton(
                  onPressed: _isLoading 
                      ? null 
                      : (_isOtpSent ? _register : _sendVerificationCode),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: brandColor,
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                  child: _isLoading 
                    ? const CircularProgressIndicator(color: Color(0xFF1C1C1C))
                    : Text(
                        _isOtpSent ? "Sign Up" : "Send Verification Code", 
                        style: const TextStyle(color: Color(0xFF1C1C1C), fontSize: 18, fontWeight: FontWeight.bold)
                      ),
                ),
              ),
              
              if (_isOtpSent && !_isLoading)
                Center(
                  child: TextButton(
                    onPressed: () {
                      setState(() {
                        _isOtpSent = false;
                        _generatedOtp = null;
                        _otpController.clear();
                        _error = null;
                      });
                    },
                    child: const Text("Change Email / Resend", style: TextStyle(color: Colors.white54)),
                  ),
                )
            ],
          ),
        ),
      ),
    );
  }

  InputDecoration _inputDecoration(String label, IconData icon, Color color) {
    return InputDecoration(
      labelText: label,
      labelStyle: const TextStyle(color: Colors.white38),
      prefixIcon: Icon(icon, color: color),
      counterText: "",
      enabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: const BorderSide(color: Colors.white10),
      ),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: BorderSide(color: color),
      ),
      disabledBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(12),
        borderSide: const BorderSide(color: Colors.white10), 
      ),
    );
  }
}