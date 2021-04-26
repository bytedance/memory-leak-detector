package com.bytedance.demo;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.View;

import com.bytedance.raphael.Raphael;
import com.bytedance.raphael.demo.R;
import com.huchao.jni.Heap;

public class MainActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.main);

        findViewById(R.id.btn_malloc).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Heap.malloc(1024);
            }
        });

        findViewById(R.id.btn).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Raphael.print();
            }
        });
    }
}